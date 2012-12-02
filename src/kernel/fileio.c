#include <kernel/fileio.h>
#include <kernel/vfs.h>
#include <kernel/fat.h>
#include <string.h>
#include <path.h>
#include <kernel/task.h>

#include <kernel/kernutil.h> /* panic */

int open(const char *path, int mode) {
	assert(mode == O_RDONLY);

	mountpoint_t *mp;
	mp = find_mountpoint_for_path(path);

	char relpath[1024] = {0};

	assert(strncmp(path, mp->path, strlen(mp->path)) == 0); // First part of the part should be the mountpoint path

	if (strcmp(mp->path, "/") == 0)
		strlcpy(relpath, path, 1024);
	else {
		// Strip the mountpoint from the beginning
		strlcpy(relpath, path + strlen(mp->path), 0);
		panic("TODO: open(): test this code path");
	}

	return mp->fops.open(mp->dev, relpath, mode); // TODO: proper checking!
}

int read(int fd, void *buf, int length) {
	assert(fd <= MAX_OPEN_FILES);

	struct open_file *file = (struct open_file *)&current_task->fdtable[fd];

	mountpoint_t *mp = file->mp;
	return mp->fops.read(fd, buf, length); // TODO: proper checking!
}

int close(int fd) {
	assert(fd <= MAX_OPEN_FILES);

	struct open_file *file = (struct open_file *)&current_task->fdtable[fd];

	mountpoint_t *mp = file->mp;
	return mp->fops.close(fd); // TODO: proper checking!
}
