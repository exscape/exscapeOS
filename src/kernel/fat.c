#include <types.h>
#include <kernel/fat.h>
#include <kernel/kernutil.h>
#include <kernel/console.h>
#include <kernel/kheap.h>
#include <kernel/list.h>
#include <string.h>
#include <kernel/partition.h>
#include <kernel/vfs.h>

/* TODO: Add more comments! */
/* TODO: FAT is case-insensitive!!! */

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

static void fat_parse_dir(DIR *dir, bool (*callback)(fat32_direntry_t *, DIR *));
//static uint32 fat_dir_num_entries(fat32_partition_t *part, uint32 cluster);
static DIR *fat_opendir_cluster(fat32_partition_t *part, uint32 cluster);

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

	return true;
}

/* Finds the next cluster in the chain, if there is one. */
static uint32 fat_next_cluster(fat32_partition_t *part, uint32 cur_cluster) {
	assert(part != NULL);
	assert(cur_cluster >= 2);

	/* Formulas taken from the FAT spec */
	uint32 fat_offset = cur_cluster * 4;
	uint32 fat_sector = part->fat_start_lba + (fat_offset / 512);
	uint32 entry_offset = fat_offset % 512;

	/* Make sure the FAT LBA is within the FAT on disk */
	assert((fat_sector >= part->fat_start_lba) && (fat_sector <= part->fat_start_lba + part->bpb->sectors_per_fat));

	/* Read the FAT sector to RAM */
	uint8 fat[512];
	assert(disk_read(part->dev, fat_sector, 512, (uint8 *)fat));

	/* Read the FAT */
	uint32 *addr = (uint32 *)(&fat[entry_offset]);
	uint32 val = (*addr) & 0x0fffffff;

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

/* Converts a short name from the directory entry (e.g. "FOO     BAR") to
 * human-readable form (e.g. "FOO.BAR") */
static void fat_parse_short_name(char *buf) {
	if (*buf == 0)
		return;

	/* Make sure this is a valid directory entry */
	assert(*buf > 0x20 || *buf == 0x05);

	/* 0x05 really means 0xE5 for the first character; make sure we don't destroy that meaning */
	if (buf[0] == 0x05)
		buf[0] = 0xe5;

	// Note: bytes 0-7 are the file name, while bytes 8-10 are the extension.
	// There's no period between them in the on-disk format, and there is
	// space padding whereever the entire length isn't used.
	// Examples: "FILENAMEEXT" -> "FILENAME.EXT"
	//           "TEST    X  " -> "TEXT.X"

	/* Store the extension */
	char ext[4] = {0};
	for (int i=0; i < 3; i++) {
		// Valid bytes are greater than 0x20 (space)
		if (buf[8 + i] > 0x20)
			ext[i] = buf[8 + i];
		else {
			ext[i] = 0;
			break;
		}
	}

	// Remove padding from the file name
	char *p = &buf[8];
	while (*(p - 1) == ' ') {
		p--;
	}

	if (ext[0] > 0x20) {
		// There's an extension; copy it over
		*p = '.';
		*(p+1) = 0;
		strlcat(p, ext, 5);
	}
	else {
		// No extension, NULL terminate here instead
		*p = 0;
	}

	return;
}

static inline bool fat_read_cluster(fat32_partition_t *part, uint32 cluster, uint8 *buffer) {
	return disk_read(part->dev, fat_lba_from_cluster(part, cluster), part->cluster_size, buffer);
}

/* Follows the cluster chain for *cur_cluster to find the next cluster
 * for this file/directory. Updates cur_cluster to the current value if successful. */
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

/* Reads a cluster (or a whole chain) from disk and figures out
 * how many files (and/or directories) it contains. */
#if 0
static uint32 fat_dir_num_entries(fat32_partition_t *part, uint32 cluster) {
	uint32 cur_cluster = cluster;
	uint8 *disk_data = kmalloc(part->cluster_size);

	uint32 num_entries = 0;

	if (cluster == part->root_dir_first_cluster)
		num_entries += 2; /* . and .. are added manually */

	/* Read the first cluster from disk */
	if (!fat_read_cluster(part, cur_cluster, disk_data)) {
		kfree(disk_data);
		return 0;
	}

	fat32_direntry_t *dir = (fat32_direntry_t *)disk_data;

	while (dir->name[0] != 0) {
		/* Run until we hit a 0x00 starting byte, signifying the end of
		 * the directory entry. */

		if ((uint8)dir->name[0] == 0xe5) {
			/* 0xe5 means unused directory entry. Try the next one. */
			goto next;
		}

		if (!(dir->attrib & ATTRIB_LFN) && !(dir->attrib & ATTRIB_VOLUME_ID)) {
			/* Don't count long file name entries or the volume ID entry;
			 * we want to know the number of files (and subdirectories),
			 * not the exact amount of entries. */
			num_entries++;
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

	return num_entries;
}
#endif

/* Locates the (first) cluster number associated with a path. */
static uint32 fat_cluster_for_path(fat32_partition_t *part, const char *in_path, uint32 type) {
	assert(part != NULL);
	assert(in_path != NULL && strlen(in_path) >= 1);
	assert(type == FS_DIRECTORY || type == FS_FILE);

	/* Take care of the simple case first... */
	if (strcmp(in_path, "/") == 0 /* && part is the root partition */)
		return part->root_dir_first_cluster;

	/* Still here? Well, life ain't always easy. */
	char path[256] = {0};
	strlcpy(path, in_path, 256);
	
	uint32 cur_cluster = part->root_dir_first_cluster;

	/*
	 * This loop walks through the path one directory at a time.
	 * For a path like /home/user/x, it will first have token equal "home",
	 * at which point cur_cluster points to the root directory.
	 * In the next loop (given that "/home" existed), token equals "user",
	 * and cur_cluster points to /home, etc. 
	 */
	char *tmp;
	char *token = NULL;
	struct dirent *dirent = NULL;
	for (token = strtok_r(path, "/", &tmp); token != NULL; token = strtok_r(NULL, "/", &tmp)) {
		/* Take care of this token */
		DIR *dir = fat_opendir_cluster(part, cur_cluster);
		dirent = fat_readdir(dir);
		while (dirent != NULL) {
			if (stricmp(dirent->d_name, token) == 0) {
				/* We found the entry we were looking for! */
				cur_cluster = dirent->d_ino;
				goto nextloop;
			}

			dirent = fat_readdir(dir);
		}
		return 0; /* File/directory not found. FIXME: errno or somesuch? */
nextloop:
		token = token; /* having this label here is an error without a statement */
	}

	if ((dirent->d_type == DT_DIR) != (type == FS_DIRECTORY)) {
		/* If the user requested a file, but this is a directory, or the other way around... */
		panic("Found a file where a directory was requested, or the other way around");
	}

	return cur_cluster;
}

/* fat_opendir helper function.
 * Returns a DIR* pointing the directory located at /cluster/. */
static DIR *fat_opendir_cluster(fat32_partition_t *part, uint32 cluster) {
	assert(part != NULL);
	assert(cluster >= 2);
	DIR *dir = kmalloc(sizeof(DIR));
	memset(dir, 0, sizeof(DIR));

	dir->partition = part;
	dir->dir_cluster = cluster;

	// These are set up in fat_readdir, when needed
	dir->buf = NULL;
	dir->pos = 0;
	dir->len = 0;

	return dir;
}

/* Used to get a list of files/subdirectories at /path/...
 * Together with readdir() and closedir(), of course. */
DIR *fat_opendir(const char *path) {
	/* Path is supposed to be relative to the partition...
	 * ... eventually. Not right now... */

	/* TODO: find the correct partition to use! */
	assert(fat32_partitions != NULL && fat32_partitions->count > 0);
	fat32_partition_t *part = (fat32_partition_t *)fat32_partitions->head->data;

	uint32 cluster = fat_cluster_for_path(part, path, FS_DIRECTORY);

	if (cluster == 0) {
		// TODO: error reporting
		return NULL;
	}

	return fat_opendir_cluster(part, cluster);
}

/* Frees the memory associated with a directory entry. */
void fat_closedir(DIR *dir) {
	if (dir == NULL)
		return;

	if (dir->buf != NULL) {
		kfree(dir->buf);
	}

	memset(dir, 0, sizeof(DIR));
	kfree(dir);

	return;
}

static bool fat_callback_create_dentries(fat32_direntry_t *disk_direntry, DIR *dir);

/* Parses a directory structure, as returned by fat_opendir().
 * Returns one entry at a time (tracking is done inside the DIR struct).
 * Returns NULL when the entire directory has been read. */
struct dirent *fat_readdir(DIR *dir) {
	// Dir is NULL, or we've read it all
	if (dir == NULL || (dir->len != 0 && dir->pos >= dir->len)) {
		assert(dir->pos == dir->len); // It shouldn't be >, really
		return NULL;
	}

	if (dir->buf == NULL) {
		/* Create the list of directory entries (this is the first call to readdir()) */
		fat_parse_dir(dir, fat_callback_create_dentries);
	}

	if (dir->buf == NULL) {
		// If we get here, fat_parse_dir failed
		// TODO: error reporting
		return NULL;
	}

	assert(dir->len > dir->pos);
	assert((dir->pos & 3) == 0);
	assert(dir->_buflen > dir->len);

	struct dirent *dent = (struct dirent *)(dir->buf + dir->pos);
	//printk("reading dent at pos %u\n", dir->pos);

	assert(dent->d_reclen != 0);

	assert((uint32)dent + sizeof(struct dirent) < ((uint32)(dir->buf + dir->_buflen)));

	dir->pos += dent->d_reclen;
	assert((dir->pos & 3) == 0);

	return dent;
}

static bool fat_callback_create_dentries(fat32_direntry_t *disk_direntry, DIR *dir) {
	if (dir->_buflen == 0) {
		// Initialize the buffer if this is the first call
		// Due to overlapping storage, this can store more than 2 entries
		dir->_buflen = sizeof(struct dirent) * 2;
		dir->buf = kmalloc(dir->_buflen);
		memset(dir->buf, 0, dir->_buflen);
	}

	UTF16_char lfn_buf[256];
	static bool have_lfn = false;
	static uint32 num_lfn_entries = 0; /* we need to know how far to access into the array */

	struct dirent *dent = NULL;

	/* Special case: pretend there's a . and .. entry for the root directory.
	 * They both link back to the root, so /./../../. == / */
	if (dir->len == 0 && dir->dir_cluster == dir->partition->root_dir_first_cluster) {
		//printk("writing dent at %u\n", dir->len);

		dent = (struct dirent *)(dir->buf + dir->len);
		strlcpy(dent->d_name, ".", DIRENT_NAME_LEN);
		dent->d_ino = 0; // this isn't a real entry on disk
		dent->d_type = DT_DIR;
		dent->d_namlen = 1;
		dent->d_reclen = 12; // 8 bytes for all but the name, plus '.' + NULL + padding
		dir->len += dent->d_reclen;

		//printk("writing dent at %u\n", dir->len);
		dent = (struct dirent *)(dir->buf + dir->len);
		strlcpy(dent->d_name, "..", DIRENT_NAME_LEN);
		dent->d_ino = 0; // this isn't a real entry on disk
		dent->d_type = DT_DIR;
		dent->d_namlen = 2;
		dent->d_reclen = 12; // 8 bytes for all but the name, plus '..' + NULL + padding
		dir->len += dent->d_reclen;
	}

	dent = (struct dirent *)(dir->buf + dir->len);

	if ((uint32)dent + sizeof(struct dirent) >= (uint32)(dir->buf + dir->_buflen)) {
		// Grow the buffer
		// Again: 2 * sizeof(struct dirent) can store much more than two
		// average-length directory entries.
		dir->_buflen += 2 * sizeof(struct dirent);
		dir->buf = krealloc(dir->buf, dir->_buflen);
		dent = (struct dirent *)(dir->buf + dir->len);
	}

	if (disk_direntry->attrib == ATTRIB_LFN) {
		/* This is a LFN entry. They are stored before the "short" (8.3) entry
		 * on disk. */

		fat32_lfn_t *lfn = (fat32_lfn_t *)disk_direntry;

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
			have_lfn = true;
		}

		UTF16_char tmp[13]; /* let's store it in one chunk, first */

		memcpy(tmp, lfn->name_1, 5 * sizeof(UTF16_char));
		memcpy(tmp + 5, lfn->name_2, 6 * sizeof(UTF16_char));
		memcpy(tmp + 5 + 6, lfn->name_3, 2 * sizeof(UTF16_char));

		/* Copy it over to the actual buffer */
		uint32 offset = ((lfn->entry & 0x3f) - 1) * 13;
		memcpy(lfn_buf + offset, tmp, 13 * sizeof(UTF16_char));
	}
	else {
		/* This is a regular directory entry. */

		/* Only used if have_lfn == true */
		char long_name_ascii[256] = {0};

		if (have_lfn) {
			/* Process LFN data! Since this is an ordinary entry, the buffer (lfn_buf)
			 * should now contain the complete long file name. */
			uint32 len = num_lfn_entries * 13; /* maximum length this might be */

			/* Convert UTF-16 -> ASCII and store in long_name_ascii */
			parse_lfn(lfn_buf, long_name_ascii, len);

			num_lfn_entries = 0;
		}

		uint32 data_cluster = ((uint32)disk_direntry->high_cluster_num << 16) | (disk_direntry->low_cluster_num);

		char short_name[13] = {0}; /* 8 + 3 + dot + NULL = 13 */
		memcpy(short_name, disk_direntry->name, 11);
		short_name[11] = 0;

		/* Convert the name to human-readable form, i.e. "FOO     BAR" -> "FOO.BAR" */
		fat_parse_short_name(short_name);

		/* Store the info! */
		if (disk_direntry->attrib & ATTRIB_DIR) {
			dent->d_type = DT_DIR;
		}
		else {
			dent->d_type = DT_REG;
		}

		/* .. to the root directory appears to be a special case. Ugh. */
		if (data_cluster == 0 && strcmp(short_name, "..") == 0)
			dent->d_ino = dir->partition->root_dir_first_cluster;
		else
			dent->d_ino = data_cluster;

		if (have_lfn) {
			strlcpy(dent->d_name, long_name_ascii, DIRENT_NAME_LEN);
			have_lfn = false; // clear for the next call
		}
		else {
			strlcpy(dent->d_name, short_name, DIRENT_NAME_LEN);
		}

		dent->d_namlen = strlen(dent->d_name);
		dent->d_reclen = 8 /* the fixed fields */ + dent->d_namlen + 1 /* NULL byte */;

		if (dent->d_reclen & 3) {
			// Pad such that the record length is divisible by 4
			dent->d_reclen &= ~3;
			dent->d_reclen += 4;
		}

		// Move the directory buffer pointer forward
		//printk("writing dent at %u\n", dir->len);
		dir->len += dent->d_reclen;
	}

	return true; // continue parsing if there are more entries
}

// Reads a FAT directory cluster trail and calls the callback with info about each
// directory entry on disk
static void fat_parse_dir(DIR *dir, bool (*callback)(fat32_direntry_t *, DIR *)) {
	assert(dir != NULL);
	assert(dir->buf == NULL);
	assert(dir->pos == 0);
	assert(dir->len == 0);
	assert(dir->dir_cluster >= 2);

	uint32 cur_cluster = dir->dir_cluster;
	//printk("cur_cluster = %u from dir->dir_cluster\n", cur_cluster);
	uint8 *disk_data = kmalloc(dir->partition->cluster_size);

	/* Read the first cluster from disk */
	assert(fat_read_cluster(dir->partition, cur_cluster, disk_data));

	fat32_direntry_t *disk_direntry = (fat32_direntry_t *)disk_data;

	while (disk_direntry->name[0] != 0) {
		/* Run until we hit a 0x00 starting byte, signifying the end of
		 * the directory entry. */

		if ((uint8)disk_direntry->name[0] == 0xe5) {
			/* 0xe5 means unused directory entry. Try the next one. */
			goto next;
		}

		/* This attribute is only valid for the volume ID (aka label) "file". */
		if (disk_direntry->attrib & ATTRIB_VOLUME_ID && disk_direntry->attrib != ATTRIB_LFN)
			goto next;

		// Do the heavy lifting elsewhere, as multiple functions - currently readdir
		// and stat - use this function.
		if (!callback(disk_direntry, dir))
			break;

	next:
		disk_direntry++;

		/* Read a new cluster if we've read past this one */
		if ((uint32)disk_direntry >= (uint32)disk_data + dir->partition->cluster_size) {
			/* Read the next cluster in this directory, if there is one */
			if (!fat_read_next_cluster(dir->partition, disk_data, &cur_cluster)) {
				/* No more clusters in this chain */
				break;
			}

			/* The new cluster is read into the same memory area, so the next entry starts back there again. */
			disk_direntry = (fat32_direntry_t *)disk_data;
		}
	}

	kfree(disk_data);
}
