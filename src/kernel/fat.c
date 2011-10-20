#include <types.h>
#include <kernel/fat.h>
#include <kernel/kernutil.h>
#include <kernel/console.h>
#include <kernel/kheap.h>
#include <kernel/list.h>
#include <string.h>

typedef struct fat32_partition {
	ata_device_t *dev; /* the device that holds this partition */
	uint32 start_lba; /* the LBA where this partition begins (past the reserved sectors) */
	uint32 end_lba; /* last valid LBA for this partition */
	uint32 cluster_start_lba;
	uint32 sectors_per_cluster;
	uint32 root_dir_first_cluster;

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
	part_info->start_lba = dev->partition[part].start_lba + bpb->reserved_sectors;
	part_info->end_lba   = dev->partition[part].start_lba + bpb->total_sectors;
	part_info->cluster_start_lba = part_info->start_lba + (bpb->num_fats * bpb->sectors_per_fat);
	part_info->sectors_per_cluster = bpb->sectors_per_cluster;
	part_info->root_dir_first_cluster = bpb->root_cluster_num;

	/* We now have no real use of the old stuff any longer */
	kfree(buf);

	/* Add the new partition entry to the list */
	list_append(fat32_partitions, part_info);

	fat_test(part_info);

	return true;
}

#define LBA_FROM_CLUSTER(p, c) ((p->cluster_start_lba + ((c - 2) * p->sectors_per_cluster)))

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

typedef struct fat32_lfn {
	uint8 entry;
	char name_1[10]; /* 5 2-byte chars of the name */
	uint8 attrib; /* Always 0xF for LFN entries */
	uint8 long_entry_type;
	uint8 checksum;
	char name_2[12]; /* the next 6 2-byte chars of the name */
	char zero[2]; /* always zero */
	char name_3[4]; /* the last 2 2-byte chars of the name */
} __attribute__((packed)) fat32_lfn_t;

static void fat_test(fat32_partition_t *part) {
	uint32 root_lba = LBA_FROM_CLUSTER(part, part->root_dir_first_cluster);
	assert(root_lba >= part->start_lba && root_lba <= part->end_lba);

	uint8 *root_data = kmalloc(128 * 512);
	assert(disk_read(part->dev, root_lba, 128, root_data));

	assert(sizeof(fat32_direntry_t) == 32);

	fat32_direntry_t *dir = (fat32_direntry_t *)root_data;

	while (dir->name[0] != 0 && (uint8)dir->name[0] != 0xe5) {
		if (dir->attrib == ATTRIB_LFN) {
			/* TODO */
		}
		else {
			printk("Found %s: %11s (%u bytes); attributes: %s%s%s%s\n",
					((dir->attrib & ATTRIB_DIR) ? "directory" : "file"),
					dir->name,
					dir->file_size,
					((dir->attrib & ATTRIB_ARCHIVE) ? "archive " : ""),
					((dir->attrib & ATTRIB_HIDDEN) ? "hidden " : ""),
					((dir->attrib & ATTRIB_SYSTEM) ? "system " : ""),
					((dir->attrib & ATTRIB_READONLY) ? "readonly " : ""));
		}
					
		dir++;
	}
}
