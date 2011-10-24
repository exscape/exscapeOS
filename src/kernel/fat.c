#include <types.h>
#include <kernel/fat.h>
#include <kernel/kernutil.h>
#include <kernel/console.h>
#include <kernel/kheap.h>
#include <kernel/list.h>
#include <string.h>
#include <kernel/part.h>
#include <kernel/vfs.h>

/* Maps on to a dir fat32_direntry_t if attrib == 0xF (ATTRIB_LFN) */
typedef uint16 UTF16_char;
typedef struct fat32_lfn {
	uint8 entry;
	UTF16_char name_1[5];
	uint8 attrib; /* Always 0xF for LFN entries */
	uint8 long_entry_type; /* should be 0 for all LFN entries */
	uint8 checksum;
	UTF16_char name_2[6];
	char zero[2]; /* always zero */
	UTF16_char name_3[2];
} __attribute__((packed)) fat32_lfn_t;

list_t *fat32_partitions = NULL;

static void fat_parse_dir(fat32_partition_t *part, uint32 cluster);

bool fat_detect(ata_device_t *dev, uint8 part) {
	/* Quite a few sanity checks */
	assert(dev != NULL);
	assert(dev->exists);
	assert(!dev->is_atapi);
	assert(part <= 3);
	assert(dev->partition[part].exists);
	assert(dev->partition[part].type == PART_FAT32 || 
	       dev->partition[part].type == PART_FAT32_LBA);
	assert(sizeof(fat32_direntry_t) == 32);
	assert(sizeof(fat32_lfn_t) == 32);
	assert(sizeof(fat32_time_t) == 2);
	assert(sizeof(fat32_date_t) == 2);

	uint8 *buf = kmalloc(512);
	/* Read the Volume ID sector */
	assert(ata_read(dev, dev->partition[part].start_lba, buf));

	assert( *( (uint16 *)(buf + 510) ) == 0xAA55);

	/* Located at the very start of the first sector on the partition */
	fat32_bpb_t *bpb = (fat32_bpb_t *)buf;

	assert(bpb->signature == 0x28 || bpb->signature == 0x29);
	assert(bpb->bytes_per_sector == 512);

	/* Create the list of partitions if it doesn't already exist) */
	if (fat32_partitions == NULL)
		fat32_partitions = list_create();
	/* Same for the mountpoints list */
	if (mountpoints == NULL)
		mountpoints = list_create();

	/* Set up an entry */
	fat32_partition_t *part_info = kmalloc(sizeof(fat32_partition_t));
	memset(part_info, 0, sizeof(fat32_partition_t));

	/* Copy over the BPB and EBPB data to the new entry */
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
	part_info->cluster_size = bpb->sectors_per_cluster * bpb->bytes_per_sector;

	/* This will need fixing later. The partition that is detected first turns in to the FS root. */
	if (mountpoints->count == 0) {
		mountpoint_t *mp = kmalloc(sizeof(mountpoint_t));
		strlcpy(mp->path, "/", sizeof(mp->path));
		mp->partition = part_info;

		list_append(mountpoints, mp);
	}
	else {
		panic("FAT partition is not root - other mountpoints are not yet supported!");
	}


	/* We now have no real use of the old stuff any longer */
	kfree(buf);

	/* Add the new partition entry to the list */
	list_append(fat32_partitions, part_info);

	fat_parse_dir(part_info, part_info->root_dir_first_cluster);

	return true;
}

/* Finds the next cluster in the chain, if there is one. */
static uint32 fat_next_cluster(fat32_partition_t *part, uint32 cur_cluster) {
	assert(part != NULL);
	assert(cur_cluster >= 2);

	/* Formulas taken from the FAT spec */
	uint32 fat_offset = cur_cluster; /* Spec has * 4, but the array is of uint32's here! */
	uint32 fat_sector = part->fat_start_lba + (fat_offset / 512);
	uint32 entry_offset = fat_offset % 512;
	assert(entry_offset <= 512/4 - 1);

	/* Make sure the FAT LBA is within the FAT on disk */
	assert((fat_sector >= part->fat_start_lba) && (fat_sector <= part->fat_start_lba + part->bpb->sectors_per_fat));

	/* Read the FAT sector to RAM */
	uint32 fat[512/sizeof(uint32)];
	assert(disk_read(part->dev, fat_sector, 512, (uint8 *)fat));

	/* Read the FAT */
	uint32 val = fat[entry_offset] & 0x0fffffff;

	return val;
}

/* Converts from the UTF-16 LFN buffer to a ASCII. Convert at most /len/ characters. */
static void parse_lfn(UTF16_char *lfn_buf, char *ascii_buf, uint32 len) {
	uint32 i = 0;
	for (i = 0; i < len; i++) {
		if (lfn_buf[i] == 0 || lfn_buf[i] == 0xffff) {
			/* Values are NULL-terminated (except where len % 13 == 0), and 0xffff-padded.
			 * If we run in to either, we've reached or passed the end. */
			ascii_buf[i] = 0;
			break;
		}
		else {
			/* This appears to be a valid UTF-16 character */
			if ( (lfn_buf[i] & 0xff80) == 0) {
				/* This character is losslessly representable as 7-bit ASCII;
				 * only the lower 7 bits are used. */
				ascii_buf[i] = lfn_buf[i] & 0x00ff;
			}
			else {
				/* This character CANNOT be represented in 7-bit ASCII.
				 * Since we have no proper Unicode support, we'll have to do this... */
				ascii_buf[i] = '_';
			}
		}
	}

	/* If len % 13 == 0, the loop will exit without NULL terminating the ASCII string. */
	ascii_buf[i] = 0;
}

/* Calculates the absolute LBA where a cluster starts on disk, given a partition and a cluster number. */
inline static uint32 fat_lba_from_cluster(fat32_partition_t *part, uint32 cluster_num) {
	return ((part->cluster_start_lba + ((cluster_num - 2) * part->sectors_per_cluster)));
}

static void fat_parse_short_name(char *buf) {
	/*
	 * I'm not proud about this function... I did try to re-think, but
	 * I didn't end up with anything much better than this.
	 */

	if (*buf == 0)
		return;

	assert(*buf > 0x20 || *buf == 0x05);

	/* Temporary arrays */
	char name[9] = {0}, ext[4] = {0};

	/* Copy the parts over */
	memcpy(name, buf, 8);
	memcpy(ext , buf + 8, 3);

	/* Trim the filename (space padded), if needed */
	char *p = (char *)&name + 7;
	while (*p == ' ' && p > (char *)&name) p--;
	*(++p) = 0;
	strcpy(buf, name);

	/* Is there an extension? */
	if (ext[0] != ' ') {
		/* Yes. */
		/* Reuse "p" */
		p = buf + strlen(buf);

		/* Add the dot, then copy the extension */
		*p++ = '.';
		for (int i=0; i < 3; i++) {
			if (ext[i] > ' ')
				*p++ = ext[i];
		}

		/* NULL terminate wherever p is pointing now */
		*p = 0;

		return;
	}
	else {
		/* No extension! That means we're done here. */
		   return;
	}
}

static inline bool fat_read_cluster(fat32_partition_t *part, uint32 cluster, uint8 *buffer) {
	return disk_read(part->dev, fat_lba_from_cluster(part, cluster), part->cluster_size, buffer);
}

static bool fat_read_next_cluster(fat32_partition_t *part, uint8 *buffer, uint32 *cur_cluster) {
	assert(part != NULL);
	assert(buffer != NULL);
	assert(cur_cluster != NULL);

	uint32 next_cluster = fat_next_cluster(part, *cur_cluster);
	if (next_cluster == 0x0ffffff7) { panic("bad cluster!"); }
	if (next_cluster >= 0x0FFFFFF8) {
		return false;
	}

	assert(next_cluster >= 2 && next_cluster < 0xfffffff7);
	*cur_cluster = next_cluster;

	/* Read this cluster from disk */
	assert(fat_read_cluster(part, *cur_cluster, buffer));

	return true;
}

static void fat_parse_dir(fat32_partition_t *part, uint32 cluster) {
	uint32 cur_cluster = cluster;
	uint8 *disk_data = kmalloc(part->cluster_size);
	fat32_direntry_t *dir = (fat32_direntry_t *)disk_data;
	dir->name[0] = 0x1; /* What do you mean, "hack"? ... Anyway, this is used to first enter the loop... */

	UTF16_char *lfn_buf = NULL; /* allocated when needed */
	uint32 num_lfn_entries = 0; /* we need to know how far to access into the array */

	/* Read the first cluster from disk */
	assert(fat_read_cluster(part, cur_cluster, disk_data));

	while (dir->name[0] != 0) {
		/* Run until we hit a 0x00 starting byte, signifying the end of
		 * the directory entry. */

		if ((uint8)dir->name[0] == 0xe5) {
			/* 0xe5 means unused directory entry. Try the next one. */
			goto next;
		}

		if (dir->attrib == ATTRIB_LFN) {
			/* This is a LFN entry. They are stored before the "short" (8.3) entry
			 * on disk. */

			fat32_lfn_t *lfn = (fat32_lfn_t *)dir;

			if (lfn->entry & 0x40) {
				/* This is the "last" LFN entry. They are stored in reverse, though,
				 * so it's the first one we encounter. */

				/* This might need some explaining...
				 * Each LFN entry stores up to 13 UTF16 chars (26 bytes).
				 * (lfn->entry & 0x3f) is how many entries there are.
				 * We need to AND away the 0x40 bit first, since it is not
				 * part of the entry count.
				 */
				num_lfn_entries = lfn->entry & 0x3f;
				lfn_buf = kmalloc(sizeof(UTF16_char) * 13 * num_lfn_entries);
			}

			UTF16_char tmp[13]; /* let's store it in one chunk, first */

			memcpy(tmp, lfn->name_1, 5 * sizeof(UTF16_char));
			memcpy(tmp + 5, lfn->name_2, 6 * sizeof(UTF16_char));
			memcpy(tmp + 5 + 6, lfn->name_3, 2 * sizeof(UTF16_char));

			/* Copy it over to the actual buffer */
			uint32 offset = ((lfn->entry & 0x3f) - 1) * 13;
			assert(lfn_buf != NULL);
			memcpy(lfn_buf + offset, tmp, 13 * sizeof(UTF16_char));
		}
		else {
			/* This is a regular directory entry. */

			/* Only used if lfn_buf != NULL */
			char *long_name_ascii = NULL;

			if (lfn_buf != NULL) {
				/* Process LFN data! */
				uint32 len = num_lfn_entries * 13; /* maximum length this might be */
				long_name_ascii = kmalloc(len + 1);

				/* Convert UTF-16 -> ASCII and store in long_name_ascii */
				parse_lfn(lfn_buf, long_name_ascii, len);

				kfree(lfn_buf);
				lfn_buf = NULL;
				num_lfn_entries = 0;
			}

			uint32 data_cluster = (dir->high_cluster_num << 16) | (dir->low_cluster_num);

			char short_name[13] = {0}; /* 8 + 3 + dot + NULL = 13 */
			memcpy(short_name, dir->name, 11);
			short_name[11] = 0;

			if (!(dir->attrib & ATTRIB_VOLUME_ID)) {
				fat_parse_short_name(short_name);
			}

			if (dir->name[0] != '.') {
				printk("%16s (short: %s) %s (%u bytes) @ %u (attribs: %s%s%s%s)\n", (long_name_ascii != NULL ? long_name_ascii : short_name), short_name,
						((dir->attrib & ATTRIB_DIR) ? "<DIR>" : ""),
						dir->file_size,
						data_cluster,
						((dir->attrib & ATTRIB_ARCHIVE) ? "A" : ""),
						((dir->attrib & ATTRIB_HIDDEN) ? "H" : ""),
						((dir->attrib & ATTRIB_SYSTEM) ? "S" : ""),
						((dir->attrib & ATTRIB_READONLY) ? "R" : ""));
			}
		}

	next:
		dir++;

		/* Read a new cluster if we've read past this one */
		if ((uint32)dir >= (uint32)disk_data + part->cluster_size) {
			/* Read the next cluster in this directory, if there is one */
			if (!fat_read_next_cluster(part, disk_data, &cur_cluster)) {
				/* No more clusters in this chain */
				break;
			}

			/* The new cluster is read into the same memory area, so the next entry starts back there again. */
			dir = (fat32_direntry_t *)disk_data;
		}
	}

	kfree(disk_data);
}
