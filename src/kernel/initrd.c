#include <kernel/initrd.h>
#include <kernel/vfs.h>
#include <sys/types.h>
#include <kernel/kernutil.h>
#include <string.h>
#include <kernel/heap.h>
#include <kernel/vfs.h>
#include <kernel/task.h>
#include <sys/errno.h>
#include <path.h>
#include <kernel/fat.h>
#include <kernel/ext2.h>

static initrd_header_t *initrd_header;     /* the initrd image header (number of files in the image) */
static initrd_file_header_t *initrd_files; /* array of headers, one for each file in the initrd */

mountpoint_t *initrd_mp = NULL;

#define INITRD_ROOT_INODE 1

/*
 * This function USES the initrd, but doesn't *really* belong here.
 * It does however use internal functions, so it'll be allowed to stay
 * for the time being.
 */
bool fs_mount(void) {
	assert(mountpoints != NULL);
	if (mountpoints->count == 0) {
		panic("Found no file systems! This should never happen as the initrd should always be there.");
	}
	int ino = -1;
	for (uint32 i=0; i < initrd_header->nfiles; i++) {
		if (strcmp(initrd_files[i].name, "mounts") == 0) {
			int parent = initrd_files[i].parent;
			if (initrd_files[parent].parent == INITRD_ROOT_INODE && strcmp(initrd_files[parent].name, "etc") == 0) {
				// Ugly, but this can't use the real initrd code.
				// Checks that this is indeed /etc/mounts and not some other file named "mounts".
				ino = i;
				break;
			}
		}
	}

	if (ino == -1) {
		panic("initrd has no /etc/mounts file! I cannot set up a FS root without it.");
	}

	initrd_file_header_t header = initrd_files[ino];
	char *buf = kmalloc(header.length + 1);
	memcpy(buf, (void *)header.offset, header.length);
	buf[header.length] = 0;

	char *p = buf;
	while (*p <= ' ' && *p != 0) p++; // skip whitespace and other junk
	assert(*p != 0);
	char mount[16] = {0};
	char path[256] = {0};
	int ap = 0;

	bool root_mounted = false;

	while (true) {
		ap = 0;
		while (*p > ' ') { mount[ap++] = *p++; }
		mount[ap] = 0;
		ap = 0;
		while (*p <= ' ' && *p != 0) p++; // skip whitespace and other junk
		assert(*p != 0);
		while (*p != '\n' && *p != '\r' && *p != 0) { path[ap++] = *p++; }
		path[ap] = 0;
		while (*p <= ' ' && *p != 0) p++; // skip whitespace and other junk

		// Calculate the "depth" of this mountpount.
		// E.g. 0 for /, 1 for /mnt, 2 for /mnt/ext2 and so on.
		int depth = 0;
		char *depth_p = path + 1;
		while (depth_p && *depth_p) {
			depth++;
			depth_p = strchr(depth_p, '/');
			if (depth_p)
				depth_p++;
		}

		list_foreach(mountpoints, it) {
			mountpoint_t *mp = (mountpoint_t *)it->data;
			mp->depth = depth;
			if (strcmp(mount, "fat") == 0) {
				if (devtable[mp->dev] != 0 && devtable[mp->dev] != (void *)0xffffffff) {
					if (strcmp(path, "/") == 0)
						root_mounted = true;
					strcpy(mp->path, path);
					fat32_partition_t *part = (fat32_partition_t *)devtable[mp->dev];
					part->mp = mp;
					printk("%s, ", mp->path);
					break;
				}
			}
			else if (strcmp(mount, "ext2") == 0) {
				if (devtable[mp->dev] != 0 && devtable[mp->dev] != (void *)0xffffffff) {
					if (strcmp(path, "/") == 0)
						root_mounted = true;
					strcpy(mp->path, path);
					ext2_partition_t *part = (ext2_partition_t *)devtable[mp->dev];
					part->mp = mp;
					printk("%s, ", mp->path);
					break;
				}
			}
			else if(strcmp(mount, "initrd") == 0) {
				if (devtable[mp->dev] == (void *)0xffffffff) {
					if (strcmp(path, "/") == 0)
						root_mounted = true;
					strcpy(mp->path, path);
					printk("%s, ", mp->path);
					initrd_mp = mp;
					break;
				}
			}
		}
		if (*p == 0)
			break;
	}

	if (!root_mounted) {
		putchar('\n');
		panic("No root file system was mounted! Make sure initrd/mounts is correct: with no FAT or ext2 filesystems, it should contain the lone line \"initrd /\" (without quotes).");
	}

	if (initrd_mp == NULL) {
		panic("initrd not mounted! Make sure there's an entry such as \"initrd /initrd\" in initrd/etc/mounts (on the build system).");
	}

	printk("\b\b: ");
	kfree(buf);
	return true;
}

int initrd_getdents(int fd, void *dp, int count);
int initrd_closedir(DIR *dir);

int initrd_read(int fd, void *buf, size_t length) {
	assert(fd <= MAX_OPEN_FILES);
	struct open_file *file = get_filp(fd);

	assert(devtable[file->dev] == (void *)0xffffffff);
	initrd_file_header_t header = initrd_files[file->ino];

	if (S_ISDIR(header.mode))
		return -EISDIR;

	/* We can't read outside the file! */
	if (file->offset >= header.length)
		return 0;

	/* Adjust the length down if needed */
	if (file->offset + length > header.length)
		length = header.length - file->offset;

	if (length == 0)
		return 0;

	assert(file->offset >= 0);
	memcpy(buf, (uint8 *)(header.offset + (uint32)file->offset), length);

	file->offset += length;

	return length;
}

int initrd_close(int fd, struct open_file *);

off_t initrd_lseek(int fd, off_t offset, int whence) {
	assert(fd <= MAX_OPEN_FILES);
	struct open_file *file = get_filp(fd);


	assert(file->ino < initrd_header->nfiles);

	if (!S_ISREG(initrd_files[file->ino].mode))
		return -EINVAL;

	uint32 file_size = initrd_files[file->ino].length;

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

	return file->offset;
}
int initrd_fstat(int fd, struct stat *st);

DIR *initrd_opendir(mountpoint_t *mp, const char *in_path);
int initrd_open(uint32 dev __attribute__((unused)), const char *path, int mode) {
	//assert(dev <= MAX_DEVS - 1);
	//assert(devtable[dev] == (void *)0xffffffff);
	/* ignore dev */
	assert(path != NULL);
	mode=mode; // still unused

	int fd;
	struct open_file *file = new_filp(&fd);
	if (!file || fd < 0)
		return -EMFILE; // TODO: huh?

	char dirname[PATH_MAX+1] = {0};
	char basename[PATH_MAX+1] = {0};
	strlcpy(dirname, path, PATH_MAX+1);
	strlcpy(basename, path, PATH_MAX+1);

	path_dirname(dirname);
	path_basename(basename);

	DIR *dir = initrd_opendir(initrd_mp, dirname);
	if (!dir) {
		destroy_filp(fd);
		return -ENOENT; // TODO: correct errno (there are other causes for opendir to fail!)
	}

	assert(dir->ino < initrd_header->nfiles);
	for (uint32 i = INITRD_ROOT_INODE; i < initrd_header->nfiles; i++) {
		if (initrd_files[i].parent == (int)dir->ino && strcmp(initrd_files[i].name, basename) == 0) {
			// Found it!
			file->ino = i;
			file->dev = dev;
			file->cur_block = i;
			file->offset = 0;
			file->size = initrd_files[i].length;
			file->mp = initrd_mp;
			file->path = strdup(path); // TODO: what does this turn out to be?
			file->fops.read  = initrd_read;
			file->fops.write = NULL;
			file->fops.close = initrd_close;
			file->fops.lseek = initrd_lseek;
			file->fops.fstat = initrd_fstat;
			if (S_ISDIR(initrd_files[i].mode))
				file->fops.getdents = initrd_getdents;
			else
				file->fops.getdents = NULL;
			file->count++;

			initrd_closedir(dir);
			return fd;
		}
	}

	destroy_filp(fd);
	initrd_closedir(dir);
	return -ENOENT;
}

int initrd_close(int fd, struct open_file *file) {
	// close() does everything required by itself

	return 0;
}

struct dirent *initrd_readdir(DIR *dir);

DIR *initrd_opendir(mountpoint_t *mp, const char *in_path) {
	assert(mp != NULL);
	assert(in_path != NULL);

	DIR *dir = kmalloc(sizeof(DIR));
	memset(dir, 0, sizeof(DIR));

	dir->dev = mp->dev;
	dir->mp = mp;

	char path[PATH_MAX+1];
	strlcpy(path, in_path, PATH_MAX+1);

	// Find the correct inode, by looping through all the tokens in the path
	char *tmp;
	char *token = NULL;
	int dir_inode = INITRD_ROOT_INODE;
	int parent_inode = INITRD_ROOT_INODE;
	for (token = strtok_r(path, "/", &tmp); token != NULL; token = strtok_r(NULL, "/", &tmp)) {
		// Did we find this token?
		// E.g. in the second loop for path="/etc/dir/file", we must find a "dir" that S_ISDIR() and is a child of etc
		bool found = false; 
		for (uint32 i = INITRD_ROOT_INODE; i < initrd_header->nfiles; i++) {
			if (initrd_files[i].parent == dir_inode && strcmp(initrd_files[i].name, token) == 0) {
				if (S_ISDIR(initrd_files[i].mode)) {
					found = true;
					dir_inode = initrd_files[i].inode;
					parent_inode = initrd_files[i].parent;
					break;
				}
				else {
					goto err; // TODO: ENOTDIR
				}
			}
		}
		if (!found)
			goto err; // TODO: ENOENT
	}

	// struct dirent has space for (currently) 256 chars in the path; the initrd only supports 64,
	// so we can cut back a bit. Add 4 bytes for padding, and the rest to ensure that the last dirent
	// is all within the buffer. Plus a bit more (32) for . and .. and a extra, just in case.
	dir->_buflen = initrd_files[dir_inode].length * (sizeof(struct dirent) - MAXNAMLEN + 64 + 4 + 32) + sizeof(struct dirent);
	dir->buf = kmalloc(dir->_buflen);
	memset(dir->buf, 0, dir->_buflen);

	dir->dops.readdir  = initrd_readdir;
	dir->dops.closedir = initrd_closedir;

	dir->pos = 0;
	dir->len = 0;
	dir->ino = dir_inode;

	// Add . and ..; very ugly, but it needs to be done...
	// The entire initrd is a bit of a hack anyway.

	struct dirent *dent = (struct dirent *)(dir->buf + dir->len);
	dent->d_ino = dir_inode;
	dent->d_dev = dir->dev;
	dent->d_type = DT_DIR;

	strcpy(dent->d_name, ".");
	dent->d_namlen = 1;
	dent->d_reclen = 12 /* fixed fields */ + dent->d_namlen + 1 /* NULL */;

	if (dent->d_reclen & 3) {
		// Pad such that the record length is divisible by 4
		dent->d_reclen &= ~3;
		dent->d_reclen += 4;
	}
	assert(dent->d_reclen <= sizeof(struct dirent));

	// Move the directory buffer pointer forward
	dir->len += dent->d_reclen;

	dent = (struct dirent *)(dir->buf + dir->len);
	dent->d_ino = parent_inode;
	dent->d_dev = dir->dev;
	dent->d_type = DT_DIR;

	strcpy(dent->d_name, "..");
	dent->d_namlen = 2;
	dent->d_reclen = 12 /* fixed fields */ + dent->d_namlen + 1 /* NULL */;

	if (dent->d_reclen & 3) {
		// Pad such that the record length is divisible by 4
		dent->d_reclen &= ~3;
		dent->d_reclen += 4;
	}
	assert(dent->d_reclen <= sizeof(struct dirent));

	// Move the directory buffer pointer forward
	dir->len += dent->d_reclen;

	// Add all of the files and folders
	for (uint32 i = INITRD_ROOT_INODE + 1; i < initrd_header->nfiles; i++) {
		if (initrd_files[i].parent != dir_inode)
			continue;

		dent = (struct dirent *)(dir->buf + dir->len);
		// It's already zeroed out, above
		dent->d_ino = i;
		dent->d_dev = dir->dev;
		if (S_ISDIR(initrd_files[i].mode))
			dent->d_type = DT_DIR;
		else
			dent->d_type = DT_REG;

		strlcpy(dent->d_name, initrd_files[i].name, 64);
		dent->d_namlen = strlen(dent->d_name);
		dent->d_reclen = 12 /* fixed fields */ + dent->d_namlen + 1 /* NULL */;

		if (dent->d_reclen & 3) {
			// Pad such that the record length is divisible by 4
			dent->d_reclen &= ~3;
			dent->d_reclen += 4;
		}

		assert(dent->d_reclen <= sizeof(struct dirent));

		// Move the directory buffer pointer forward
		dir->len += dent->d_reclen;
	}

	return dir;

err:
	kfree(dir);
	return NULL;
}

struct dirent *initrd_readdir(DIR *dir) {
	// Dir is NULL, or we've read it all
	if (dir == NULL || (dir->len != 0 && dir->pos >= dir->len)) {
		assert(dir->pos == dir->len); // Anything but exactly equal is a bug
		return NULL;
	}
	assert(dir->buf != NULL);

	assert(dir->len > dir->pos);
	assert((dir->pos & 3) == 0);
	assert(dir->_buflen > dir->len);
	struct dirent *dent = (struct dirent *)(dir->buf + dir->pos);

	assert(dent->d_reclen != 0);

	assert((uint32)dent + sizeof(struct dirent) < ((uint32)(dir->buf + dir->_buflen)));

	dir->pos += dent->d_reclen;
	assert((dir->pos & 3) == 0);

	return dent;
}

/* Frees the memory associated with a directory entry. */
int initrd_closedir(DIR *dir) {
	if (dir == NULL)
		return -EINVAL; // TODO: what should closedir return here?

	if (dir->buf != NULL) {
		kfree(dir->buf);
	}

	memset(dir, 0, sizeof(DIR));
	kfree(dir);

	return 0;
}

int initrd_stat(mountpoint_t *mp, const char *in_path, struct stat *st);

int initrd_fstat(int fd, struct stat *st) {
	struct open_file *file = get_filp(fd);
	assert(file != NULL);

	char relpath[PATH_MAX+1] = {0};
	find_relpath(file->path, relpath, NULL);

	return initrd_stat(file->mp, relpath, st);
}

int initrd_stat(mountpoint_t *mp, const char *in_path, struct stat *st) {
	assert(mp != NULL);
	assert(in_path != NULL);
	assert(st != NULL);

	int fd = initrd_open(0xffff /* invalid dev, unused */, in_path, 0);
	if (fd < 0)
		return fd;

	struct open_file *file = get_filp(fd);
	assert(file != NULL);

	memset(st, 0, sizeof(struct stat));

	st->st_dev = 0xffff; // invalid ID
	for (int dev=0; dev < MAX_DEVS; dev++) {
		if (devtable[dev] == (void *)0xffffffff) {
			st->st_dev = dev;
			break;
		}
	}

	int i = file->ino;

	uint32 blocks = (initrd_files[i].length % 4096 == 0) ? initrd_files[i].length / 4096 : (initrd_files[i].length / 4096) + 1;
	st->st_ino = i;
	st->st_mode = initrd_files[i].mode;
	st->st_nlink = 1;
	st->st_size = initrd_files[i].length;
	st->st_mtime = initrd_files[i].mtime;
	st->st_ctime = initrd_files[i].mtime;
	st->st_atime = initrd_files[i].mtime;
	st->st_blksize = 4096; // Doesn't matter
	st->st_blocks = blocks;

	close(fd);

	return 0;
}

int initrd_getdents(int fd, void *dp, int count) {
	// Fill upp /dp/ with at most /count/ bytes of struct dirents, read from the
	// open directory with descriptor /fd/. We need to keep track of where we are,
	// since the caller doesn't do that via the parameters.

	if (count < 128) {
		return -EINVAL;
	}

	struct open_file *file = get_filp(fd);
	assert(file != NULL);

	if (file->data == NULL) {
		// This directory was just opened, and we haven't actually fetched any entries yet! Do so.
		assert(file->dev < MAX_DEVS);
		assert(file->path != NULL && file->path[0] != 0);
		file->data = initrd_opendir(initrd_mp, file->path);
	}

	DIR *dir = file->data;
	assert(dir != NULL);

	if (dir->buf != NULL && dir->pos >= dir->len) {
		// We're done!
		assert(dir->pos == dir->len); // Anything but exactly equal is a bug
		initrd_closedir(dir);
		file->data = NULL;
		// TODO: is this cleanup enough?
		return 0;
	}

	if (dir->buf == NULL) {
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

/* Fetches the initrd from the location specified (provided by GRUB),
 * and sets up the necessary structures. */
void init_initrd(uint32 location) {
	initrd_header = (initrd_header_t *)location;
	initrd_files = (initrd_file_header_t *)(location + sizeof(initrd_header_t));

	mountpoint_t *mp = kmalloc(sizeof(mountpoint_t));
	//strlcpy(mp->path, "/", sizeof(mp->path));

	memset(mp, 0, sizeof(mountpoint_t));

	mp->path[0] = 0; // not set up here
	mp->mpops.open     = initrd_open;
	mp->mpops.opendir  = initrd_opendir;
	mp->mpops.stat     = initrd_stat;

	mp->dev = next_dev; // increased below

	if (mountpoints == NULL)
		mountpoints = list_create();

	list_append(mountpoints, mp);

	devtable[next_dev++] = (void *)0xffffffff; // We don't need a useful pointer, just something to mark the entry as used

	/* Set up each individual file */
	for (uint32 i = INITRD_ROOT_INODE; i < initrd_header->nfiles; i++) {
		/* Change the offset value to be relative to the start of memory, 
		 * rather than the start of the ramdisk/initrd image */
		initrd_files[i].offset += location;
	}
}
