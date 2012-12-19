#include <sys/types.h>
#include <kernel/fat.h>
#include <kernel/kernutil.h>
#include <kernel/console.h>
#include <kernel/heap.h>
#include <kernel/list.h>
#include <string.h>
#include <kernel/partition.h>
#include <kernel/vfs.h>
#include <path.h>
#include <sys/errno.h>
#include <kernel/pmm.h>
#include <kernel/time.h>

/* TODO: Add more comments! */
/* TODO: FAT is case-insensitive!!! */

struct dir /* aka DIR */ *fat_opendir(mountpoint_t *mp, const char *path);
struct dirent *fat_readdir(struct dir *dir);
int fat_closedir(struct dir *dir);
int fat_fstat(int fd, struct stat *buf);
int fat_open(uint32 dev, const char *path, int mode);
int fat_read(int fd, void *buf, size_t length);
int fat_close(int fd);

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

#define min(a,b) ( (a < b ? a : b) )

static void fat_parse_dir(DIR *dir, bool (*callback)(fat32_direntry_t *, DIR *, char *, void *), void *);
//static uint32 fat_dir_num_entries(fat32_partition_t *part, uint32 cluster);
static DIR *fat_opendir_cluster(fat32_partition_t *part, uint32 cluster, mountpoint_t *mp);
static uint32 fat_cluster_for_path(fat32_partition_t *part, const char *in_path, int type);
static inline bool fat_read_cluster(fat32_partition_t *part, uint32 cluster, uint8 *buffer);
static uint32 fat_next_cluster(fat32_partition_t *part, uint32 cur_cluster);
int fat_stat(mountpoint_t *mp, const char *in_path, struct stat *buf);
int fat_getdents (int fd, void *dp, int count);

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

	part_info->magic = FAT32_MAGIC;

	/* Copy over the BPB and EBPB data to the new entry */
	part_info->bpb = kmalloc(sizeof(fat32_bpb_t));
	memcpy(part_info->bpb, bpb, sizeof(fat32_bpb_t));

	/* Set up the other struct variables */
	part_info->dev = dev; /* the device that holds this partition */
	part_info->fat_start_lba = dev->partition[part].start_lba + bpb->reserved_sectors;
	part_info->end_lba = dev->partition[part].start_lba + bpb->total_sectors;
	part_info->cluster_start_lba = part_info->fat_start_lba + (bpb->num_fats * bpb->sectors_per_fat);
	part_info->sectors_per_cluster = bpb->sectors_per_cluster;
	part_info->root_dir_first_cluster = bpb->root_cluster_num;
	part_info->part_info = &dev->partition[part];
	part_info->cluster_size = bpb->sectors_per_cluster * bpb->bytes_per_sector;

	// Set up the mountpoint
	mountpoint_t *mp = kmalloc(sizeof(mountpoint_t));
	mp->path[0] = 0; // not set up here
	mp->mpops.open     = fat_open;
	mp->mpops.opendir  = fat_opendir;
	mp->mpops.stat     = fat_stat;
	mp->dev = next_dev; // increased below
	list_append(mountpoints, mp);

	// Store this in the device table (used for dev ID number -> partition mappings)
	devtable[next_dev++] = (void *)part_info;

	/* We now have no real use of the old stuff any longer */
	kfree(buf);

	/* Add the new partition entry to the list */
	list_append(fat32_partitions, part_info);

	// Cache the entire FAT in RAM. TODO: don't do this if it wouldn't fit
	uint32 fat_bytes = part_info->bpb->sectors_per_fat * 512;
	if (fat_bytes % 512) {
		// disk_read only reads full sectors!
		fat_bytes %= 512;
		fat_bytes += 512;
	}

	if (fat_bytes < pmm_bytes_free() / 2) {
		// Only cache if there's at least *some* RAM to spare for it
		part_info->cached_fat = kmalloc(fat_bytes);
		assert(disk_read(part_info->dev, part_info->fat_start_lba, fat_bytes, part_info->cached_fat));
	}
	else
		part_info->cached_fat = NULL;

	return true;
}

/* Calculates the absolute LBA where a cluster starts on disk, given a partition and a cluster number. */
inline static uint32 fat_lba_from_cluster(fat32_partition_t *part, uint32 cluster_num) {
	return ((part->cluster_start_lba + ((cluster_num - 2) * part->sectors_per_cluster)));
}

off_t fat_lseek(int fd, off_t offset, int whence) {
	struct open_file *file = (struct open_file *)&current_task->fdtable[fd];

	assert(file->ino >= 2);
	assert(file->count > 0);

	struct stat st;
	uint32 file_size;
	if (fat_fstat(fd, &st) == 0)
		file_size = st.st_size;
	else {
		return -EINVAL; // TODO: proper errno (when stat has that)
	}

	if (whence == SEEK_SET) {
		if (offset < 0)
			return -EINVAL;

		file->offset = offset;
	}
	else if (whence == SEEK_CUR) {
		if (offset + file->offset < 0)
			return -EINVAL;

		file->offset += offset;
	}
	else if (whence == SEEK_END) {
		if (file_size + offset < 0)
			return -EINVAL;

		file->offset = file_size + offset;
	}
	else
		return -EINVAL; // invalid whence value

	assert(file->offset >= 0);

	if (file->offset < file_size) {
		// We can seek to this location; update _cur_ino (which stores the inode to read from at the current offset)

		fat32_partition_t *part = devtable[file->dev];
		assert(part != NULL);
		assert(part->magic == FAT32_MAGIC);

		assert(st.st_dev == file->dev);
		assert(st.st_ino == file->ino);

		uint32 local_offset = (uint32)file->offset;

		file->_cur_ino = file->ino; // TODO: only start from the first cluster/inode if truly necessary

		while (local_offset >= part->cluster_size) {
			file->_cur_ino = fat_next_cluster(part, file->_cur_ino);
			if (file->_cur_ino >= 0x0ffffff8) {
				// End of cluster chain
				panic("lseek: seek beyond file ending despite checks to make this impossible - bug in fat_lseek");
				return -EINVAL; // not reached
			}
			local_offset -= part->cluster_size;
		}
	}

	return file->offset;
}

int fat_open(uint32 dev, const char *path, int mode) {
	assert(dev <= MAX_DEVS - 1);
	assert(devtable[dev] != NULL);
	assert(path != NULL);
	mode=mode; // still unused

	int fd = get_free_fd();
	if (fd < 0)
		return -EMFILE;

	struct open_file *file = (struct open_file *)&current_task->fdtable[fd];

	fat32_partition_t *part = (fat32_partition_t *)devtable[dev];
	assert(part != NULL);
	assert(part->magic == FAT32_MAGIC);

	uint32 cluster = fat_cluster_for_path(part, path, 0 /* any type */);

	if (cluster <= 0x0ffffff6) {
		file->dev = dev;
		file->ino = cluster;
		file->_cur_ino = cluster;
		file->offset = 0;
		file->size = 0; // TODO: should this be kept or not?
		file->mp = NULL;
		file->fops.read  = fat_read;
		file->fops.write = NULL;
		file->fops.close = fat_close;
		file->fops.lseek = fat_lseek;
		file->fops.fstat = fat_fstat;
		file->fops.getdents = fat_getdents;
		for (node_t *it = mountpoints->head; it != NULL; it = it->next) {
			mountpoint_t *mp = (mountpoint_t *)it->data;
			if (mp->dev == dev) {
				file->mp = mp;
				break;
			}
		}
		file->path = strdup(path);
		file->count++;
		assert(file->count == 1); // We have no dup, dup2 etc. yet

		assert(file->mp != NULL);

		return fd;
	}
	else {
		return -ENOENT;
	}
}

int fat_read(int fd, void *buf, size_t length) {
	struct open_file *file = (struct open_file *)&current_task->fdtable[fd];

	if (file->ino == 0 && file->count != 0) {
		// This file appears to be empty (FAT cluster 0 means a size-0 file). Let's verify.
		struct stat st;
		if (fat_fstat(fd, &st) == 0 && st.st_size == 0) {
			// Yup!
			return 0;
		}
		else {
			panic("file->ino == 0 on opened file");
		}
	}

	fat32_partition_t *part = devtable[file->dev];
	assert(part != NULL);
	assert(part->magic == FAT32_MAGIC);

	uint8 *cluster_buf = kmalloc(part->cluster_size * 32);

	if (file->size == 0) {
		// Size is not initialized; this should be the first read. Set it up.
		struct stat st;
		fat_fstat(fd, &st);
		assert(st.st_dev == file->dev);
		assert(st.st_ino == file->ino);

		if (S_ISDIR(st.st_mode)) {
			return -EISDIR;
		}

		file->size = st.st_size;
	}

	uint32 bytes_read = 0; // this call to read() only

	if (file->offset >= file->size) {
		goto done;
	}

	assert(file->offset >= 0);
	uint32 local_offset = (uint32)file->offset % part->cluster_size;

	uint32 continuous_clusters = 1;
	do {
		if (length > part->cluster_size) {
			// The request is for more than one cluster, so at least two need to be
			// read from disk; if they are continuous on disk, we can read them faster
			// by coalescing them into a single disk request.
			uint32 next = 0, cur = file->_cur_ino;
			while ((next = fat_next_cluster(part, cur)) == cur + 1 && \
					continuous_clusters * part->cluster_size < length && \
					continuous_clusters < 32)
			{
				cur = next;
				continuous_clusters++;
			}
		}

		uint32 nbytes_read_from_disk = continuous_clusters * part->cluster_size;

		assert(disk_read(part->dev, fat_lba_from_cluster(part, file->_cur_ino), nbytes_read_from_disk, cluster_buf));
		file->_cur_ino += continuous_clusters - 1; // the last one is taken care of later in all cases

		// We need to stop if either the file size is up, or if the user didn't want more bytes.
		uint32 bytes_copied = min(min(file->size - file->offset, length), nbytes_read_from_disk);

		if (bytes_copied >= nbytes_read_from_disk - local_offset) {
			// We'd read outside the buffer we've read from disk! Limit this read size.
			bytes_copied = nbytes_read_from_disk - local_offset;
		}

		// Copy the data to the buffer
		memcpy((void *)( (uint8 *)buf + bytes_read), cluster_buf + local_offset, bytes_copied);

		bytes_read += bytes_copied;
		file->offset += bytes_copied;
		local_offset += bytes_copied;

		assert(file->offset <= file->size);

		assert(length >= bytes_copied);
		length -= bytes_copied;

		if (local_offset >= part->cluster_size) {
			file->_cur_ino = fat_next_cluster(part, file->_cur_ino);
			assert(file->_cur_ino > 2);
			if (file->_cur_ino >= 0x0ffffff8) {
				// End of cluster chain
				assert(file->offset == file->size);
				goto done;
			}
		}
		while (local_offset >= part->cluster_size) {
			local_offset -= part->cluster_size;
		}

		if (length == 0 || file->offset >= file->size)
			break;

	} while (length > 0);

done:
	kfree(cluster_buf);
	return bytes_read;
}

int fat_close(int fd) {
	struct open_file *file = (struct open_file *)&current_task->fdtable[fd];
	assert(file->count != 0);

	kfree(file->path);
	file->path = NULL;

	return 0;
}

/* Finds the next cluster in the chain, if there is one. */
static uint32 fat_next_cluster(fat32_partition_t *part, uint32 cur_cluster) {
	assert(part != NULL);
	assert(cur_cluster >= 2);
	uint32 val = 0;
	if (part->cached_fat) {
		// FAT is cached; read from RAM
		uint32 *fat = (uint32 *)part->cached_fat;
		val = fat[cur_cluster];
	}
	else {
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
		val = *(uint32 *)(&fat[entry_offset]);
	}

	return (val & 0x0fffffff);
}

/* Converts from the UTF-16 LFN buffer to a ASCII. Convert at most /len/ characters. */
static void parse_lfn(UTF16_char *lfn_buf, char *ascii_buf) {
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
static void fat_parse_short_name(char *buf, const char *name) {
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

	assert(next_cluster >= 2 && next_cluster < 0x0ffffff7);
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
static uint32 fat_cluster_for_path(fat32_partition_t *part, const char *in_path, int type) {
	assert(part != NULL);
	assert(in_path != NULL && strlen(in_path) >= 1);

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
		DIR *dir = fat_opendir_cluster(part, cur_cluster, find_mountpoint_for_path(in_path));
		while ((dirent = fat_readdir(dir)) != NULL) {
			if (stricmp(dirent->d_name, token) == 0) {
				/* We found the entry we were looking for! */
				if (type == DT_DIR && dirent->d_type != DT_DIR) {
					fat_closedir(dir);
					return (uint32)(-ENOTDIR);
				}
				cur_cluster = dirent->d_ino;
				goto nextloop;
			}
		}
		fat_closedir(dir);
		return 0xffffffff; /* we didn't find anything! */
nextloop:
		fat_closedir(dir);
	}

	return cur_cluster;
}

/* fat_opendir helper function.
 * Returns a DIR* pointing the directory located at /cluster/. */
static DIR *fat_opendir_cluster(fat32_partition_t *part, uint32 cluster, mountpoint_t *mp) {
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

static bool fat_callback_create_dentries(fat32_direntry_t *disk_direntry, DIR *dir, char *, void *);
static bool fat_callback_stat(fat32_direntry_t *disk_direntry, DIR *dir, char *, void *);

struct stat_callback_data {
	struct stat *st;
	char *file;
	bool success;
};

int fat_stat(mountpoint_t *mp, const char *in_path, struct stat *buf) {
	assert(in_path != NULL);
	assert(buf != NULL);
	assert(mp != NULL);

	size_t path_len = strlen(in_path);
	char *path = kmalloc(path_len + 1);
	char *base = kmalloc(path_len + 1);
	strcpy(path, in_path);
	strcpy(base, in_path);

	// Store the path and "file" (possibly directory) names separately
	path_dirname(path);
	path_basename(base);

	if (strcmp(path, "/") == 0 && strcmp(base, "/") == 0) {
		// SPECIAL CASE: stat the root directory, which
		// won't be found in the directory list on-disk. Ugh.
		memset(buf, 0, sizeof(struct stat));

		buf->st_dev = mp->dev;
		buf->st_ino = 2;
		buf->st_mode = 0777; // TODO
		buf->st_mode |= 040000; // directory
		buf->st_nlink = 1;
		buf->st_size = 0;
		// TODO: set times!
		buf->st_blksize = 4096;
		buf->st_blocks = 1;

		return 0;
	}

	DIR *dir = fat_opendir(mp, path);
	if (!dir) {
		// TODO: errno
		goto error;
	}

	struct stat_callback_data data;
	data.st = buf;
	data.file = base; // what file (or directory) to stat()
	data.success = false;

	memset(buf, 0, sizeof(struct stat));

	fat_parse_dir(dir, fat_callback_stat, &data);
	if (data.success) {
		// The callback updated the struct successfully
		fat_closedir(dir);
		kfree(path);
		kfree(base);
		return 0;
	}
	else {
		// TODO: errno
		goto error;
	}
error:
	kfree(path);
	kfree(base);
	if (dir)
		fat_closedir(dir);
	return -1;
}

int fat_fstat(int fd, struct stat *buf) {
	// Ugh, this feels like a huge hack.
	struct open_file *file = (struct open_file *)&current_task->fdtable[fd];

	char relpath[PATH_MAX+1] = {0};
	find_relpath(file->path, relpath, NULL);

	return fat_stat(file->mp, relpath, buf);
}
#define FAT_MTIME 0
#define FAT_CTIME 1
#define FAT_ATIME 2

static time_t fat_calc_time(fat32_direntry_t *direntry, int type) {
	/*
DIRENTRY:
	 fat32_time_t create_time;
    fat32_date_t create_date;

    fat32_date_t access_date;

    uint16 high_cluster_num;

    fat32_time_t mod_time;
    fat32_date_t mod_date;

typedef struct fat32_time {
    uint16 second : 5;
    uint16 minute : 6;
    uint16 hour : 5;
} __attribute__((packed)) fat32_time_t;

// Date format used in fat32_direntry_t. Relative to 1980-01-01
typedef struct fat32_date {
    uint16 day : 5;
    uint16 month : 4;
    uint16 year : 7;
} __attribute__((packed)) fat32_date_t;

*/
	fat32_date_t *fdate;
	fat32_time_t *ftime;

	switch (type) {
		case FAT_MTIME:
			fdate = &direntry->mod_date;
			ftime = &direntry->mod_time;
			break;
		case FAT_CTIME:
			fdate = &direntry->create_date;
			ftime = &direntry->create_time;
			break;
		case FAT_ATIME:
			/* FAT doesn't store access *time*, only date... */
			fdate = &direntry->access_date;
			ftime = NULL;
			break;
		default:
			panic("Invalid parameter to fat_calc_time");
	}

	Time ts;
	memset(&ts, 0, sizeof(Time));
	ts.year = (1980 + fdate->year) - 1900;
	ts.month = fdate->month - 1;
	ts.day = fdate->day;
	if (ftime != NULL) {
		ts.hour = ftime->hour;
		ts.minute = ftime->minute;
		ts.second = ftime->second * 2;
	}

	return kern_mktime(&ts);
}

static bool fat_callback_stat(fat32_direntry_t *disk_direntry, DIR *dir, char *lfn_buf, void *in_data) {
	assert(disk_direntry != NULL);
	assert(dir != NULL);
	assert(in_data != NULL);

	struct stat_callback_data *data = (struct stat_callback_data *)in_data;

	char name[256] = {0};
	if (lfn_buf)
		strlcpy(name, lfn_buf, 256);
	else
		fat_parse_short_name(name, disk_direntry->name);

	struct stat *st = data->st;
	fat32_partition_t *part = devtable[dir->dev];
	//printk("fat_callback_stat: name %s, file %s\n", name, data->file);

	if (stricmp(name, data->file) == 0) {
		// We found it! We can finally fill in the struct stat.

		uint32 num_blocks = 0;
		if (!(disk_direntry->attrib & ATTRIB_DIR)) {
			// Calculate how many clusters this file uses. If the file size is evenly
			// divisible into the cluster size, that's the count. If not, an additional cluster
			// is used for the last part of the file.
		}

		memset(st, 0, sizeof(struct stat));

		st->st_dev = 0xffff; // invalid ID
		for (int i=0; i < MAX_DEVS; i++) {
			if ((fat32_partition_t *)devtable[i] == part) {
				st->st_dev = i;
				break;
			}
		}
		st->st_ino = (disk_direntry->high_cluster_num << 16) | (disk_direntry->low_cluster_num);
		st->st_mode = 0777; // TODO
		if (disk_direntry->attrib & ATTRIB_DIR)
			st->st_mode |= 040000;
		st->st_nlink = 1;
		st->st_size = (disk_direntry->attrib & ATTRIB_DIR) ? 0 : disk_direntry->file_size;
		st->st_mtime = fat_calc_time(disk_direntry, FAT_MTIME);
		st->st_ctime = fat_calc_time(disk_direntry, FAT_CTIME);
		st->st_atime = fat_calc_time(disk_direntry, FAT_ATIME);
		st->st_blksize = (part->cluster_size > 16384 ? part->cluster_size : 16384);
		num_blocks = disk_direntry->file_size / st->st_blksize;
		if (disk_direntry->file_size % st->st_blksize != 0) {
			num_blocks += 1;
		}
		st->st_blocks = (disk_direntry->attrib & ATTRIB_DIR) ? 1 : num_blocks;

		data->success = true;

		return false; // Break the directory parsing, since we've found what we wanted
	}

	return true; // keep looking
}

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

int fat_getdents (int fd, void *dp, int count) {
	// Fill upp /dp/ with at most /count/ bytes of struct dirents, read from the
	// open directory with descriptor /fd/. We need to keep track of where we are,
	// since the caller doesn't do that via the parameters.
	// TODO: read stuff as-needed from the file system, instead of reading EVERYTHING
	// at once. That was the easy solution as I already had the parsing in place prior
	// to deciding to use getdents() for user mode...

	if (count < 128) {
		return -EINVAL;
	}

	struct open_file *file = (struct open_file *)&current_task->fdtable[fd];

	if (file->data == NULL) {
		// This directory was just opened, and we haven't actually fetched any entries yet! Do so.
		assert(file->dev < MAX_DEVS);
		fat32_partition_t *part = (fat32_partition_t *)devtable[file->dev];
		assert(part != NULL);
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
static bool fat_callback_create_dentries(fat32_direntry_t *disk_direntry, DIR *dir, char *lfn_buf, void *data __attribute__((unused))) {
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
static void fat_parse_dir(DIR *dir, bool (*callback)(fat32_direntry_t *, DIR *, char *, void *), void *data) {
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
	assert(fat_read_cluster(part, cur_cluster, disk_data));

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
