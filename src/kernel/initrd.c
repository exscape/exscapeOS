#include <kernel/initrd.h>
#include <kernel/vfs.h>
#include <types.h>
#include <kernel/kernutil.h>
#include <string.h>
#include <kernel/heap.h>
#include <kernel/vfs.h>
#include <kernel/task.h>

static initrd_header_t *initrd_header;     /* the initrd image header (number of files in the image) */
static initrd_file_header_t *file_headers; /* array of headers, one for each file in the initrd */

//struct dirent dirent;

// Returns the file size
//static uint32 initrd_fsize(fs_node_t *node) {
//initrd_file_header_t header = file_headers[node->inode];
//return header.length;
//}

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
		if (strcmp(file_headers[i].name, "mounts") == 0) {
			ino = i;
			break;
		}
	}

	if (ino == -1) {
		panic("initrd has no \"mounts\" file! I cannot set up a FS root without it.");
	}

	initrd_file_header_t header = file_headers[ino];
	char *buf = kmalloc(header.length);
	memcpy(buf, (void *)header.offset, header.length);

	char *p = buf;
	while (*p <= ' ' && *p != 0) p++; // skip whitespace and other junk
	assert(*p != 0);
	char mount[16] = {0};
	char path[256] = {0};
	int ap = 0;

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

		for (node_t *it = mountpoints->head; it != NULL; it = it->next) {
			mountpoint_t *mp = (mountpoint_t *)it->data;
			if (strcmp(mount, "fat") == 0) {
				if (devtable[mp->dev] != 0 && devtable[mp->dev] != (void *)0xffffffff) {
					strcpy(mp->path, path);
					printk("%s, ", mp->path);
					break;
				}
			}
			else if(strcmp(mount, "initrd") == 0) {
				if (devtable[mp->dev] == (void *)0xffffffff) {
					strcpy(mp->path, path);
					printk("%s, ", mp->path);
					break;
				}
			}
		}
		if (*p == 0)
			break;
	}

	printk("\b\b: ");
	kfree(buf);
	return true;
}


int initrd_read(int fd, void *buf, size_t length) {
	assert(fd <= MAX_OPEN_FILES);
	struct open_file *file = (struct open_file *)&current_task->fdtable[fd];
	assert(devtable[file->dev] == (void *)0xffffffff);
	initrd_file_header_t header = file_headers[file->ino];

	/* We can't read outside the file! */
	if (file->offset >= header.length)
		return 0;

	/* Adjust the length down if needed */
	if (file->offset + length > header.length)
		length = header.length - file->offset;

	if (length == 0)
		return 0;

	memcpy(buf, (uint8 *)(header.offset + file->offset), length);

	file->offset += length;

	return length;
}

int initrd_open(uint32 dev, const char *path, int mode) {
	assert(dev <= MAX_DEVS - 1);
	assert(devtable[dev] == (void *)0xffffffff);
	assert(path != NULL);
	mode=mode; // still unused

	const char *p = strchr(path, '/');
	if (p == path) { // Path begins with a /
		p++;
		p = strchr(p, '/');
		assert(p == NULL); // path much not contain directories
		p = path + 1;
	}
	else
		p = path;

	assert(current_task->_next_fd + 1 <= MAX_OPEN_FILES);
	struct open_file *file = (struct open_file *)&current_task->fdtable[current_task->_next_fd++];

	file->ino = 0xffffffff;
	// Find the inode number
	// TODO: use finddir here
	for (uint32 i = 0; i < initrd_header->nfiles; i++) {
		if (strcmp(file_headers[i].name, p) == 0) {
			file->ino = i;
			file->dev = dev;
			file->_cur_ino = i;
			file->offset = 0;
			file->size = 0; // TODO: remove?
			file->mp = NULL;
			file->path = strdup(path); // TODO: what does this turn out to be?
			for (node_t *it = mountpoints->head; it != NULL; it = it->next) {
				mountpoint_t *mp = (mountpoint_t *)it->data;
				if (mp->dev == dev) {
					file->mp = mp;
					break;
				}
			}
			break;
		}
	}

	if (file->ino == 0xffffffff) {
		// We didn't find it
		// TODO: errno
		return -1;
	}

	assert(file->mp != NULL);

	return current_task->_next_fd - 1;
}

int initrd_close(int fd) {
	assert(fd <= MAX_OPEN_FILES);
	struct open_file *file = (struct open_file *)&current_task->fdtable[fd];
	assert(file->dev <= MAX_DEVS);
	assert(devtable[file->dev] == (void *)0xffffffff);

	if (file->path)
		kfree(file->path);
	memset(file, 0, sizeof(struct open_file));

	return 0;
}

DIR *initrd_opendir(mountpoint_t *mp, const char *path) {
	assert(mp != NULL);
	assert(path != NULL);
	assert(strcmp(path, "/") == 0); // initrd doesn't support subdirectories!

	DIR *dir = kmalloc(sizeof(DIR));
	memset(dir, 0, sizeof(DIR));

	dir->dev = mp->dev;
	dir->mp = mp;

	// struct dirent has space for (currently) 256 chars in the path; the initrd only supports 64,
	// so we can cut back a bit. Add 4 bytes for padding, and the rest to ensure that the last dirent
	// is all within the buffer.
	dir->_buflen = initrd_header->nfiles * (sizeof(struct dirent) - DIRENT_NAME_LEN + 64 + 4) + sizeof(struct dirent);
	dir->buf = kmalloc(dir->_buflen);
	memset(dir->buf, 0, dir->_buflen);

	dir->pos = 0;
	dir->len = 0;

	for (uint32 i = 0; i < initrd_header->nfiles; i++) {
		struct dirent *dent = (struct dirent *)(dir->buf + dir->len);
		// It's already zeroed out, above
		dent->d_ino = i;
		dent->d_dev = dir->dev;
		dent->d_type = DT_REG;
		strlcpy(dent->d_name, file_headers[i].name, 64);
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
		return -1;

	if (dir->buf != NULL) {
		kfree(dir->buf);
	}

	memset(dir, 0, sizeof(DIR));
	kfree(dir);

	return 0;
}

int initrd_stat(mountpoint_t *mp, const char *in_path, struct stat *st) {
	assert(mp != NULL);
	assert(in_path != NULL);
	assert(st != NULL);

	const char *p = NULL;
	if (in_path[0] == '/') {
		p = &in_path[1];
		if (!strchr(p, '/') == NULL) {
			panic("initrd_stat: initrd does't support subdirectories!");
			return -1;
		}
	}

	for (uint32 i = 0; i < initrd_header->nfiles; i++) {
		if (strcmp(file_headers[i].name, p) == 0) {
			memset(st, 0, sizeof(struct stat));

			st->st_dev = 0xffff; // invalid ID
			for (int dev=0; dev < MAX_DEVS; dev++) {
				if (devtable[dev] == (void *)0xffffffff) {
					st->st_dev = dev;
					break;
				}
			}
			uint32 blocks = (file_headers[i].length % 4096 == 0) ? file_headers[i].length / 4096 : (file_headers[i].length / 4096) + 1;
			st->st_ino = i;
			st->st_mode = 0777; // TODO
			st->st_nlink = 1;
			st->st_size = file_headers[i].length;
			st->st_atime = 0;
			st->st_ctime = 0;
			st->st_mtime = 0;
			st->st_blksize = 4096; // Doesn't matter
			st->st_blocks = blocks;

			return 0;
		}
	}

	return -1;
}

/* Fetches the initrd from the location specified (provided by GRUB),
 * and sets up the necessary structures. */
void init_initrd(uint32 location) {
	initrd_header = (initrd_header_t *)location;
	file_headers = (initrd_file_header_t *)(location + sizeof(initrd_header_t));

	mountpoint_t *mp = kmalloc(sizeof(mountpoint_t));
	//strlcpy(mp->path, "/", sizeof(mp->path));

	memset(mp, 0, sizeof(mountpoint_t));

	mp->path[0] = 0; // not set up here
	mp->fops.open     = initrd_open;
	mp->fops.read     = initrd_read;
	mp->fops.write    = NULL;
	mp->fops.close    = initrd_close;
	mp->fops.opendir  = initrd_opendir;
	mp->fops.readdir  = initrd_readdir;
	mp->fops.closedir = initrd_closedir;
	mp->fops.stat     = initrd_stat;

	mp->dev = next_dev; // increased below

	if (mountpoints == NULL)
		mountpoints = list_create();

	list_append(mountpoints, mp);

	devtable[next_dev++] = (void *)0xffffffff; // We don't need a useful pointer, just something to mark the entry as used

	/* Set up each individual file */
	for (uint32 i = 0; i < initrd_header->nfiles; i++) {
		/* Change the offset value to be relative to the start of memory, 
		 * rather than the start of the ramdisk/initrd image */
		file_headers[i].offset += location;
	}
}
