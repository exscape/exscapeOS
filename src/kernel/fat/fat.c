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
int fat_close(int fd, struct open_file *file);
static bool fat_callback_stat(fat32_direntry_t *disk_direntry, DIR *dir, char *lfn_buf, void *in_data);
void fat_parse_dir(DIR *dir, bool (*callback)(fat32_direntry_t *, DIR *, char *, void *), void *data);
void fat_parse_short_name(char *buf, const char *name);

struct stat_callback_data {
	struct stat *st;
	char *file;
	bool success;
	int _errno;
};

list_t *fat32_partitions = NULL;

#define min(a,b) ( (a < b ? a : b) )

DIR *fat_opendir_cluster(fat32_partition_t *part, uint32 cluster, mountpoint_t *mp);
uint32 fat_cluster_for_path(fat32_partition_t *part, const char *in_path, int type);
bool fat_read_cluster(fat32_partition_t *part, uint32 cluster, uint8 *buffer);
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
	assert(dev->partition[part].type == PART_FAT32 || \
	       dev->partition[part].type == PART_FAT32_LBA);
	assert(sizeof(fat32_direntry_t) == 32);
	assert(sizeof(fat32_lfn_t) == 32);
	assert(sizeof(fat32_time_t) == 2);
	assert(sizeof(fat32_date_t) == 2);

	uint8 *buf = kmalloc(512);
	/* Read the Volume ID sector */
	int ret = ata_read(dev, dev->partition[part].start_lba, buf, 1);
	assert(ret != 0);

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
	memset(mp, 0, sizeof(mountpoint_t));
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

	uint32 fat_bytes = part_info->bpb->sectors_per_fat * 512;
	if (fat_bytes % 512) {
		// disk_read only reads full sectors!
		fat_bytes %= 512;
		fat_bytes += 512;
	}

	if (fat_bytes < pmm_bytes_free() / 2) {
		// Cache the FAT in RAM if there's some room to spare
		part_info->cached_fat = kmalloc(fat_bytes);
		ret = disk_read(part_info->dev, part_info->fat_start_lba, fat_bytes, part_info->cached_fat);
		assert(ret != 0);
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
	struct open_file *file = get_filp(fd);

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
		// We can seek to this location; update cur_block (which stores the inode to read from at the current offset)

		fat32_partition_t *part = devtable[file->dev];
		assert(part != NULL);
		assert(part->magic == FAT32_MAGIC);

		assert(st.st_dev == file->dev);
		assert(st.st_ino == file->ino);

		uint32 local_offset = (uint32)file->offset;

		file->cur_block = file->ino; // TODO: only start from the first cluster/inode if truly necessary

		while (local_offset >= part->cluster_size) {
			file->cur_block = fat_next_cluster(part, file->cur_block);
			if (file->cur_block >= 0x0ffffff8) {
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

	int fd;
	struct open_file *file = new_filp(&fd);
	if (!file || fd < 0)
		return -EMFILE;

	fat32_partition_t *part = (fat32_partition_t *)devtable[dev];
	assert(part != NULL);
	assert(part->magic == FAT32_MAGIC);

	uint32 cluster = fat_cluster_for_path(part, path, 0 /* any type */);

	if (cluster <= 0x0ffffff6) {
		file->dev = dev;
		file->ino = cluster;
		file->cur_block = cluster;
		file->offset = 0;
		file->size = 0; // TODO: should this be kept or not?
		file->mp = NULL;
		file->fops.read  = fat_read;
		file->fops.write = NULL;
		file->fops.close = fat_close;
		file->fops.lseek = fat_lseek;
		file->fops.fstat = fat_fstat;
		file->fops.getdents = fat_getdents;
		list_foreach(mountpoints, it) {
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
		destroy_filp(fd);
		return -ENOENT;
	}
}

int fat_read(int fd, void *buf, size_t length) {
	struct open_file *file = get_filp(fd);

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

	const uint32 max_clusters = 32; // How many clusters to read at once (to keep the buffer size down)

	uint8 *cluster_buf = kmalloc(part->cluster_size * max_clusters);

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
			uint32 next = 0, cur = file->cur_block;
			while ((next = fat_next_cluster(part, cur)) == cur + 1 && \
					continuous_clusters * part->cluster_size < length && \
					continuous_clusters < max_clusters)
			{
				cur = next;
				continuous_clusters++;
			}
		}

		uint32 nbytes_read_from_disk = continuous_clusters * part->cluster_size;

		int ret = disk_read(part->dev, fat_lba_from_cluster(part, file->cur_block), nbytes_read_from_disk, cluster_buf);
		assert(ret != 0);
		file->cur_block += continuous_clusters - 1; // the last one is taken care of later in all cases

		// We need to stop if either the file size is up, or if the user didn't want more bytes.
		uint32 bytes_copied = min(min(file->size - file->offset, (off_t)length), nbytes_read_from_disk);

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
			ino_t next = fat_next_cluster(part, file->cur_block);
			if (file->cur_block >= 0x0ffffff8 || file->cur_block < 2) {
				// End of cluster chain / no data
				assert(file->offset == file->size);
				goto done;
			}
			else
				file->cur_block = next;
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

int fat_close(int fd, struct open_file *file) {
	// close() does everything required by itself.

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
		int ret = disk_read(part->dev, fat_sector, 512, (uint8 *)fat);
		assert(ret != 0);

		/* Read the FAT */
		val = *(uint32 *)(&fat[entry_offset]);
	}

	return (val & 0x0fffffff);
}
bool fat_read_cluster(fat32_partition_t *part, uint32 cluster, uint8 *buffer) {
	return disk_read(part->dev, fat_lba_from_cluster(part, cluster), part->cluster_size, buffer);
}

/* Follows the cluster chain for *cur_cluster to find the next cluster
 * for this file/directory. Updates cur_cluster to the current value if successful. */
bool fat_read_next_cluster(fat32_partition_t *part, uint8 *buffer, uint32 *cur_cluster) {
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
	int ret = fat_read_cluster(part, *cur_cluster, buffer);
	assert(ret != 0);

	return true;
}

/* Locates the (first) cluster number associated with a path. */
uint32 fat_cluster_for_path(fat32_partition_t *part, const char *in_path, int type) {
	assert(part != NULL);
	assert(in_path != NULL && strlen(in_path) >= 1);

	/* Take care of the simple case first... */
	if (strcmp(in_path, "/") == 0)
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
		DIR *dir = fat_opendir_cluster(part, cur_cluster, part->mp);
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

		kfree(path);
		kfree(base);
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

	return -ENOENT; // TODO: FIXME
}

int fat_fstat(int fd, struct stat *buf) {
	// Ugh, this feels like a huge hack.
	struct open_file *file = get_filp(fd);

	char relpath[PATH_MAX+1] = {0};
	find_relpath(file->path, relpath, NULL);

	return fat_stat(file->mp, relpath, buf);
}

#define FAT_MTIME 0
#define FAT_CTIME 1
#define FAT_ATIME 2

time_t fat_calc_time(fat32_direntry_t *direntry, int type) {
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

	Time ts = {0};
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
			st->st_mode |= _IFDIR;
		else
			st->st_mode |= _IFREG;
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
