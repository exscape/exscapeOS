#include <sys/types.h>
#include <kernel/fat.h>
#include <kernel/kernutil.h>
#include <kernel/console.h>
#include <kernel/heap.h>
//#include <kernel/list.h>
#include <string.h>
#include <kernel/partition.h>
#include <kernel/vfs.h>
#include <path.h>
#include <sys/errno.h>
#include <kernel/pmm.h>
#include <kernel/time.h>

struct dirent *fat_readdir(DIR *dir);
int fat_closedir(DIR *dir);
int fat_getdents(int fd, void *dp, int count);
uint32 fat_cluster_for_path(fat32_partition_t *part, const char *in_path, int type);
bool fat_read_cluster(fat32_partition_t *part, uint32 cluster, uint8 *buffer);
bool fat_read_next_cluster(fat32_partition_t *part, uint8 *buffer, uint32 *cur_cluster);
void fat_parse_dir(DIR *dir, bool (*callback)(fat32_direntry_t *, DIR *, char *, void *), void *data);

/* Converts from the UTF-16 LFN buffer to a ASCII. Convert at most /len/ characters. */
void parse_lfn(UTF16_char *lfn_buf, char *ascii_buf) {
	uint32 i = 0;
	for (i = 0; i < 256; i++) {
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

	ascii_buf[i] = 0;
}

/* Converts a short name from the directory entry (e.g. "FOO     BAR") to
 * human-readable form (e.g. "FOO.BAR") */
void fat_parse_short_name(char *buf, const char *name) {
	if (*name == 0)
		return;

	/* Make sure this is a valid directory entry */
	assert(*name > 0x20 || *name == 0x05);

	memcpy(buf, name, 11);
	memset(buf + 11, 0, 2);

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

/* fat_opendir helper function.
 * Returns a DIR* pointing the directory located at /cluster/. */
DIR *fat_opendir_cluster(fat32_partition_t *part, uint32 cluster, mountpoint_t *mp) {
	assert(part != NULL);
	assert(cluster >= 2);
	assert(mp != NULL);
	DIR *dir = kmalloc(sizeof(DIR));
	memset(dir, 0, sizeof(DIR));

	dir->ino = cluster;
	dir->mp = mp;
	dir->dev = mp->dev;

	dir->dops.readdir  = fat_readdir;
	dir->dops.closedir = fat_closedir;

	// These are set up in fat_readdir, when needed
	dir->buf = NULL;
	dir->pos = 0;
	dir->len = 0;

	return dir;
}

bool fat_callback_create_dentries(fat32_direntry_t *disk_direntry, DIR *dir, char *, void *);

/* Used to get a list of files/subdirectories at /path/...
 * Together with readdir() and closedir(), of course. */
DIR *fat_opendir(mountpoint_t *mp, const char *path) {
	assert(mp != NULL);
	assert(path != NULL);
	assert(mp->dev <= MAX_DEVS);
	assert(devtable[mp->dev] != 0 && devtable[mp->dev] != (void *)0xffffffff);
	assert(strlen(path) > 0);

	/* Path is supposed to be relative to the partition...
	 * ... eventually. Not right now... TODO */

	fat32_partition_t *part = devtable[mp->dev];

	uint32 cluster = fat_cluster_for_path(part, path, DT_DIR);

	if (cluster == 0 || cluster >= 0x0fffffff) {
		// TODO: error reporting (-ENOENT, -ENOTDIR)
		return NULL;
	}

	return fat_opendir_cluster(part, cluster, mp);
}

/* Frees the memory associated with a directory entry. */
int fat_closedir(DIR *dir) {
	if (dir == NULL)
		return -1;

	if (dir->buf != NULL) {
		kfree(dir->buf);
	}

	memset(dir, 0, sizeof(DIR));
	kfree(dir);

	return 0;
}

int fat_getdents(int fd, void *dp, int count) {
	// Fill upp /dp/ with at most /count/ bytes of struct dirents, read from the
	// open directory with descriptor /fd/. We need to keep track of where we are,
	// since the caller doesn't do that via the parameters.
	// TODO: read stuff as-needed from the file system, instead of reading EVERYTHING
	// at once. That was the easy solution as I already had the parsing in place prior
	// to deciding to use getdents() for user mode...

	if (count < 128) {
		return -EINVAL;
	}

	struct open_file *file = get_filp(fd);

	if (file->data == NULL) {
		// This directory was just opened, and we haven't actually fetched any entries yet! Do so.
		assert(file->dev < MAX_DEVS);
		fat32_partition_t *part = (fat32_partition_t *)devtable[file->dev];
		assert(part != NULL);

		struct stat st;
		fstat(fd, &st);
		if (!S_ISDIR(st.st_mode))
			return -ENOTDIR;

		file->data = fat_opendir_cluster(part, file->ino, file->mp);
		if (file->data == NULL) {
			return -ENOTDIR; // TODO - this can't happen at the moment, though (fat_opendir_cluster always succeds)
		}
	}

	DIR *dir = file->data;
	assert(dir != NULL);

	if (dir->len != 0 && dir->pos >= dir->len) {
		// We're done!
		assert(dir->pos == dir->len); // Anything but exactly equal is a bug
		closedir(dir);
		file->data = NULL;
		// TODO: is this cleanup enough?
		return 0;
	}

	if (dir->buf == NULL) {
		/* Create the list of directory entries */
		fat_parse_dir(dir, fat_callback_create_dentries, NULL /* callback-specific data, not used for this one */);
	}

	if (dir->buf == NULL) {
		// If we get here, fat_parse_dir failed
		// TODO: error reporting
		return -1;
	}

	assert(dir->len > dir->pos);
	assert((dir->pos & 3) == 0);
	assert(dir->_buflen > dir->len);

	// Okay, time to get to work!
	// buffer to copy to is /dp/, and at most /count/ bytes can be written safely
	int written = 0;
	while (dir->pos < dir->len) {
		struct dirent *dent = (struct dirent *)(dir->buf + dir->pos);
		if (written + dent->d_reclen > count) {
			if (dent->d_reclen > count) {
				// Won't fit the next time around, either!
				return -EINVAL; // TODO: memory leak if we don't clean up somewhere
			}
			else {
				// This won't fit! Read it the next time around.
				return written;
			}
		}
		assert((char *)dp + written + dent->d_reclen <= (char *)dp + count);

		// Okay, this entry fits; copy it over.
		memcpy((char *)dp + written, dent, dent->d_reclen);
		written += dent->d_reclen;

		dir->pos += dent->d_reclen;
		assert((dir->pos & 3) == 0);
	}

	// If we get here, the loop exited due to there being no more entries,
	// though the caller can receive more still. Ensure the state is consistent,
	// and return.
	assert(written != 0); // we shouldn't get here if nothing was written
	assert(dir->pos == dir->len);

	return written;
}

/* Parses a directory structure, as returned by fat_opendir().
 * Returns one entry at a time (tracking is done inside the DIR struct).
 * Returns NULL when the entire directory has been read. */
struct dirent *fat_readdir(DIR *dir) {
	// Dir is NULL, or we've read it all
	if (dir == NULL || (dir->len != 0 && dir->pos >= dir->len)) {
		assert(dir->pos == dir->len); // Anything but exactly equal is a bug
		return NULL;
	}

	if (dir->buf == NULL) {
		/* Create the list of directory entries (this is the first call to readdir()) */
		fat_parse_dir(dir, fat_callback_create_dentries, NULL /* callback-specific data, not used for this one */);
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

// Used with fat_parse_dir to read a directory structure into a DIR struct,
// which is then used by readdir() to return struct dirents to the caller.
bool fat_callback_create_dentries(fat32_direntry_t *disk_direntry, DIR *dir, char *lfn_buf, void *data __attribute__((unused))) {
	if (dir->_buflen == 0) {
		// Initialize the buffer if this is the first call
		// Due to overlapping storage, this can store more than 2 entries
		dir->_buflen = sizeof(struct dirent) * 2;
		dir->buf = kmalloc(dir->_buflen);
		memset(dir->buf, 0, dir->_buflen);
	}

	struct dirent *dent = NULL;
	assert(dir->dev <= MAX_DEVS);
	fat32_partition_t *part = devtable[dir->dev];
	assert(part != (void *)0xffffffff && part != NULL);

	// TODO: don't do this inside the callback (which is looped)!
	uint16 dev = 0xffff; // invalid value
	for (int i=0; i < MAX_DEVS; i++) {
		if ((fat32_partition_t *)devtable[i] == part) {
			dev = i;
			break;
		}
	}

	/* Special case: pretend there's a . and .. entry for the root directory.
	 * They both link back to the root, so /./../../. == / */
	if (dir->len == 0 && dir->ino == part->root_dir_first_cluster) {
		//printk("writing dent at %u\n", dir->len);

		dent = (struct dirent *)(dir->buf + dir->len);
		strlcpy(dent->d_name, ".", MAXNAMLEN);
		dent->d_ino = part->root_dir_first_cluster;
		dent->d_dev = dev;
		dent->d_type = DT_DIR;
		dent->d_namlen = 1;
		dent->d_reclen = 16; // 12 bytes for all but the name, plus '.' + NULL + padding
		dir->len += dent->d_reclen;

		//printk("writing dent at %u\n", dir->len);
		dent = (struct dirent *)(dir->buf + dir->len);
		strlcpy(dent->d_name, "..", MAXNAMLEN);
		dent->d_ino = part->root_dir_first_cluster;
		dent->d_dev = dev;
		dent->d_type = DT_DIR;
		dent->d_namlen = 2;
		dent->d_reclen = 16; // 12 bytes for all but the name, plus '..' + NULL + padding
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

	uint32 data_cluster = ((uint32)disk_direntry->high_cluster_num << 16) | (disk_direntry->low_cluster_num);

	char short_name[13] = {0}; /* 8 + 3 + dot + NULL = 13 */
	fat_parse_short_name(short_name, disk_direntry->name);

	/* Store the info! */
	if (disk_direntry->attrib & ATTRIB_DIR) {
		dent->d_type = DT_DIR;
	}
	else {
		dent->d_type = DT_REG;
	}

	/* .. to the root directory appears to be a special case. Ugh. */
	if (data_cluster == 0 && strcmp(short_name, "..") == 0)
		dent->d_ino = part->root_dir_first_cluster;
	else
		dent->d_ino = data_cluster;

	dent->d_dev = dev;

	if (lfn_buf) {
		strlcpy(dent->d_name, lfn_buf, MAXNAMLEN);
	}
	else {
		strlcpy(dent->d_name, short_name, MAXNAMLEN);
	}

	dent->d_namlen = strlen(dent->d_name);
	dent->d_reclen = 12 /* the fixed fields */ + dent->d_namlen + 1 /* NULL byte */;

	if (dent->d_reclen & 3) {
		// Pad such that the record length is divisible by 4
		dent->d_reclen &= ~3;
		dent->d_reclen += 4;
	}

	assert(dent->d_reclen <= sizeof(struct dirent));

	// Move the directory buffer pointer forward
	//printk("writing dent at %u\n", dir->len);
	dir->len += dent->d_reclen;

	return true; // continue parsing if there are more entries
}

// Reads a FAT directory cluster trail and calls the callback with info about each
// directory entry on disk
void fat_parse_dir(DIR *dir, bool (*callback)(fat32_direntry_t *, DIR *, char *, void *), void *data) {
	assert(dir != NULL);
	assert(dir->buf == NULL);
	assert(dir->pos == 0);
	assert(dir->len == 0);
	assert(dir->ino >= 2);

	uint32 cur_cluster = dir->ino;
	//printk("cur_cluster = %u from dir->ino\n", cur_cluster);
	assert(dir->dev <= MAX_DEVS);
	fat32_partition_t *part = devtable[dir->dev];
	assert(part != (void *)0xffffffff && part != NULL);

	uint8 *disk_data = kmalloc(part->cluster_size);

	/* Read the first cluster from disk */
	int ret = fat_read_cluster(part, cur_cluster, disk_data);
	assert(ret != 0);

	fat32_direntry_t *disk_direntry = (fat32_direntry_t *)disk_data;

	static UTF16_char lfn_buf[256];
	static bool have_lfn = false;
	static uint8 lfn_entry_num = 0; /* we need to know how far to access into the array */

	while (disk_direntry->name[0] != 0) {
		/* Run until we hit a 0x00 starting byte, signifying the end of
		 * the directory entries. */

		if ((uint8)disk_direntry->name[0] == 0xe5) {
			/* 0xe5 means unused directory entry. Try the next one. */
			goto next;
		}

		/* This attribute is only valid for the volume ID (aka label) "file". */
		if (disk_direntry->attrib & ATTRIB_VOLUME_ID && disk_direntry->attrib != ATTRIB_LFN)
			goto next;

		if (disk_direntry->attrib == ATTRIB_LFN) {
			/* This is a LFN entry. They are stored before the "short" (8.3) entry
			 * on disk. */

			fat32_lfn_t *lfn = (fat32_lfn_t *)disk_direntry;

			if (lfn->entry & 0x40) {
				/* This is the "last" LFN entry. They are stored in reverse, though,
				 * so it's the first one we encounter. */

				have_lfn = true;
				memset(lfn_buf, 0, sizeof(lfn_buf));
			}

			/* This might need some explaining...
			 * Each LFN entry stores up to 13 UTF16 chars (26 bytes).
			 * (lfn->entry & 0x3f) is how many entries there are.
			 * We need to AND away the 0x40 bit first, since it is not
			 * part of the entry count.
			 */
			lfn_entry_num = lfn->entry & 0x3f;

			UTF16_char tmp[13]; /* let's store it in one chunk, first */

			memcpy(tmp, lfn->name_1, 5 * sizeof(UTF16_char));
			memcpy(tmp + 5, lfn->name_2, 6 * sizeof(UTF16_char));
			memcpy(tmp + 5 + 6, lfn->name_3, 2 * sizeof(UTF16_char));

			/* Copy it over to the actual buffer */
			uint32 offset = (lfn_entry_num - 1) * 13;
			memcpy(lfn_buf + offset, tmp, 13 * sizeof(UTF16_char));

			goto next;
		}
		else {
			char long_name_ascii[256] = {0};
			if (have_lfn)
				parse_lfn(lfn_buf, long_name_ascii);
			// Do the heavy lifting elsewhere, as multiple functions - currently readdir
			// and stat - use this function.
			if (!callback(disk_direntry, dir, (have_lfn ? long_name_ascii : NULL), data))
				break;

			have_lfn = false;
		}

	next:
		disk_direntry++;

		/* Read a new cluster if we've read past this one */
		if ((uint32)disk_direntry >= (uint32)disk_data + part->cluster_size) {
			/* Read the next cluster in this directory, if there is one */
			if (!fat_read_next_cluster(part, disk_data, &cur_cluster)) {
				/* No more clusters in this chain */
				break;
			}
			//printk("cur_cluster = %u from fat_read_next_cluster\n", cur_cluster);

			/* The new cluster is read into the same memory area, so the next entry starts back there again. */
			disk_direntry = (fat32_direntry_t *)disk_data;
		}
	}

	kfree(disk_data);
}
