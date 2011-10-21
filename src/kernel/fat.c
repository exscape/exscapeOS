#include <types.h>
#include <kernel/fat.h>
#include <kernel/kernutil.h>
#include <kernel/console.h>
#include <kernel/kheap.h>
#include <kernel/list.h>
#include <string.h>
#include <kernel/part.h>

typedef struct fat32_partition {
	ata_device_t *dev; /* the device that holds this partition */
	uint32 fat_start_lba; /* the LBA where the FAT begins */
	uint32 end_lba; /* last valid LBA for this partition */
	uint32 cluster_start_lba;
	uint32 sectors_per_cluster;
	uint32 root_dir_first_cluster;

	/* TODO: better naming. This is a pointer back to the dev->partition[x] structure. */
	partition_t *part_info;

	/* The entire BPB and EBPB data structures for this partition */
	fat32_bpb_t *bpb;
} fat32_partition_t;

list_t *fat32_partitions = NULL;

static void fat_test(fat32_partition_t *part);

bool fat_detect(ata_device_t *dev, uint8 part) {
	/* Quite a few sanity checks */
	assert(dev != NULL);
	assert(dev->exists);
	assert(!dev->is_atapi);
	assert(part <= 3);
	assert(dev->partition[part].exists);
	assert(dev->partition[part].type == PART_FAT32 || 
	       dev->partition[part].type == PART_FAT32_LBA);

	uint8 *buf = kmalloc(512);
	/* Read the Volume ID sector */
	assert(ata_read(dev, dev->partition[part].start_lba, buf));

	assert( *( (uint16 *)(buf + 510) ) == 0xAA55);

	/* Located at the very start of the first sector on the partition */
	fat32_bpb_t *bpb = (fat32_bpb_t *)buf;

	assert(bpb->signature == 0x28 || bpb->signature == 0x29);

	/* Create the list of partitions if it doesn't already exist) */
	if (fat32_partitions == NULL)
		fat32_partitions = list_create();

	/* Set up an entry */
	fat32_partition_t *part_info = kmalloc(sizeof(fat32_partition_t));
	memset(part_info, 0, sizeof(fat32_partition_t));

	/* Copy over the BPB and EBPB structs to the new entry */
	part_info->bpb = kmalloc(sizeof(fat32_bpb_t));
	memcpy(part_info->bpb, bpb, sizeof(fat32_bpb_t));

	/* Set up the other struct variables */
	part_info->dev = dev; /* the device that holds this partition */
	part_info->fat_start_lba = dev->partition[part].start_lba + bpb->reserved_sectors;
	part_info->end_lba   = dev->partition[part].start_lba + bpb->total_sectors;
	part_info->cluster_start_lba = part_info->fat_start_lba + (bpb->num_fats * bpb->sectors_per_fat);
	part_info->sectors_per_cluster = bpb->sectors_per_cluster;
	part_info->root_dir_first_cluster = bpb->root_cluster_num;
	part_info->part_info = &dev->partition[part];

	/* We now have no real use of the old stuff any longer */
	kfree(buf);

	/* Add the new partition entry to the list */
	list_append(fat32_partitions, part_info);

	fat_test(part_info);

	return true;
}

typedef struct fat32_time {
	uint16 second : 5;
	uint16 minute : 6;
	uint16 hour : 5;
} __attribute__((packed)) fat32_time_t;

typedef struct fat32_date {
	uint16 day : 5;
	uint16 month : 4;
	uint16 year : 7;
} __attribute__((packed)) fat32_date_t;

typedef struct fat32_direntry {
	char name[11]; /* 8.3 file name; the 8 part is space padded if shorter */
	uint8 attrib;
	uint8 reserved;
	uint8 created_10ths;

	fat32_time_t create_time;
	fat32_date_t create_date;

	fat32_date_t access_date;

	uint16 high_cluster_num;

	fat32_time_t mod_time;
	fat32_date_t mod_date;

	uint16 low_cluster_num;
	uint32 file_size;
} __attribute__((packed)) fat32_direntry_t;

typedef uint16 UTF16_char;

typedef struct fat32_lfn {
	uint8 entry;
	//char name_1[10]; /* 5 2-byte chars of the name */
	UTF16_char name_1[5];
	uint8 attrib; /* Always 0xF for LFN entries */
	uint8 long_entry_type; /* should be 0 for all LFN entries */
	uint8 checksum;
	//char name_2[12]; /* the next 6 2-byte chars of the name */
	UTF16_char name_2[6];
	char zero[2]; /* always zero */
	//char name_3[4]; /* the last 2 2-byte chars of the name */
	UTF16_char name_3[2];
} __attribute__((packed)) fat32_lfn_t;

/* Finds the next cluster in the chain, if there is one. */
static uint32 fat_next_cluster(fat32_partition_t *part, uint32 cur_cluster) {
	assert(part != NULL);
	const uint32 cluster_size = part->bpb->sectors_per_cluster * 512;
	uint32 *fat = kmalloc(cluster_size);

	/* Formulas taken from the FAT spec */
	uint32 fat_offset = cur_cluster * 4; // sizeof(uint32)
	uint32 fat_sector = part->fat_start_lba + (fat_offset / 512);
	uint32 entry_offset = fat_offset % 512;

	/* Make sure the FAT LBA is within the FAT on disk */
	assert((fat_sector >= part->fat_start_lba) && (fat_sector <= part->fat_start_lba + (part->bpb->sectors_per_fat * 512)));

	/* Read it */
	assert(disk_read(part->dev, fat_sector, cluster_size, (uint8 *)fat));

	assert(entry_offset < (part->bpb->sectors_per_fat * 512)/4);

	/* Read the FAT */
	uint32 val = fat[entry_offset] & 0x0fffffff;

	kfree(fat);

	return val;
}

static void fat_test(fat32_partition_t *part) {

#define IN_MEM(addr) ( (uint32)addr >= (uint32)disk_data && (uint32)addr < (uint32)disk_data + (part->sectors_per_cluster*512) )

#define LBA_FROM_CLUSTER(p, c) ((p->cluster_start_lba + ((c - 2) * p->sectors_per_cluster)))

	const uint32 cluster_size = part->sectors_per_cluster * 512;
	assert(sizeof(fat32_direntry_t) == 32);

	uint32 cur_cluster = part->root_dir_first_cluster;

	uint8 *disk_data = kmalloc(cluster_size);

	while (true) {

		assert(disk_read(part->dev, LBA_FROM_CLUSTER(part, cur_cluster), cluster_size, disk_data));

		fat32_direntry_t *dir = (fat32_direntry_t *)disk_data;

		UTF16_char *lfn_buf = NULL; /* allocated when needed */
		uint32 num_lfn_entries = 0; /* we need to know how far to access into the array */

		while (dir->name[0] != 0) {
			if ((uint8)dir->name[0] == 0xe5) {
				goto next;
			}
			if (dir->attrib == ATTRIB_LFN) {
				fat32_lfn_t *lfn = (fat32_lfn_t *)dir;
				printk("LFN entry! Entry = %02x\n", lfn->entry);

				if (lfn->entry & 0x40) {
					/* This is the "last" LFN entry. They are stored in reverse, though,
					 * so it's the first one we encounter. */

					/* This might need some explaining...
					 * Each LFN entry stores up to 13 UTF16 chars (26 bytes).
					 * (lfn->entry & 0x3f) is how many entries there are.
					 * We need to AND away the 0x40 bit first, since it is not
					 * part of the "how many LFN entries are there" info.
					 */
					num_lfn_entries = lfn->entry & 0x3f;
					lfn_buf = kmalloc(sizeof(UTF16_char) * 13 * num_lfn_entries);
				}

				UTF16_char tmp[13]; /* let's store it in one chunk, first */

				memcpy(tmp, lfn->name_1, 5 * sizeof(UTF16_char));
				memcpy(tmp + 5, lfn->name_2, 6 * sizeof(UTF16_char));
				memcpy(tmp + 5 + 6, lfn->name_3, 2 * sizeof(UTF16_char));

				for (int i=0; i < 13; i++)
					if (tmp[i] != 0 && tmp[i] != 0xffff) {
						/* If this is always true, converting to 7-bit ASCII is super easy. */
						assert((tmp[i] & 0xff80) == 0);
					}

				/* Copy it over to the actual buffer */
				uint32 offset = ((lfn->entry & 0x3f) - 1) * 13;
				memcpy(lfn_buf + offset, tmp, 13 * sizeof(UTF16_char));
			}
			else {
				if (lfn_buf != NULL) {
					/* Process LFN data! */
					uint32 len = num_lfn_entries * 13; /* maximum length this might be */
					printk("Found dir entry with LFN data in buffer! (Up to) %u chars.\n", len);

					char *ascii_buf = kmalloc(len + 1);

					uint32 i = 0;
					for (i = 0; i < len; i++) {
						if (lfn_buf[i] == 0 || lfn_buf[i] == 0xffff) {
							ascii_buf[i] = 0;
							break;
						}
						else {
							if ( (lfn_buf[i] & 0xff80) == 0) {
								ascii_buf[i] = lfn_buf[i] & 0x00ff;
							}
							else
								panic("Unsupported character in LFN!");
						}
					}
					ascii_buf[i] = 0;

					printk("LFN filename is: %s\n", ascii_buf);


					kfree(lfn_buf);
					lfn_buf = NULL;
					num_lfn_entries = 0;
				}

				printk("Found %s: %11s (%u bytes); attributes: %s%s%s%s\n",
						((dir->attrib & ATTRIB_DIR) ? "directory" : "file"),
						dir->name,
						dir->file_size,
						((dir->attrib & ATTRIB_ARCHIVE) ? "archive " : ""),
						((dir->attrib & ATTRIB_HIDDEN) ? "hidden " : ""),
						((dir->attrib & ATTRIB_SYSTEM) ? "system " : ""),
						((dir->attrib & ATTRIB_READONLY) ? "readonly " : ""));
			}

		next:
			dir++;
			if (!IN_MEM(dir))
				panic("next entry not in RAM!");

		} /* end inner loop; time to go to the next cluster */

		/* Look for the next cluster in the chain */
		uint32 next_cluster = fat_next_cluster(part, cur_cluster);
		printk("cur_cluster = %u, next = %u\n", cur_cluster, next_cluster);
		if (next_cluster == 0x0ffffff7) { panic("bad cluster!"); }
		if (next_cluster >= 0x0FFFFFF8) {
			panic("END! No more clusters in this chain!");
		}
		cur_cluster = next_cluster;
	} /* end outer loop */

	kfree(disk_data);
	panic("end");
}
