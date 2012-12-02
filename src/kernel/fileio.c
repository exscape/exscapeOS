#include <kernel/fileio.h>
#include <kernel/vfs.h>
#include <kernel/fat.h>
#include <string.h>
#include <path.h>
#include <kernel/task.h>

#include <kernel/kernutil.h> /* panic */

int open(const char *path, int mode) {
	assert(mode == O_RDONLY);
	assert(path != NULL);

	if (path[0] != '/')
		return -1; // only absolute paths are supported

	mountpoint_t *mp;
	mp = find_mountpoint_for_path(path);
	assert(mp != NULL);

	char relpath[1024] = {0};

	assert(strncmp(path, mp->path, strlen(mp->path)) == 0); // First part of the part should be the mountpoint path

	if (strcmp(mp->path, "/") == 0)
		strlcpy(relpath, path, 1024);
	else {
		// Strip the mountpoint from the beginning
		strlcpy(relpath, path + strlen(mp->path), 0);
		panic("TODO: open(): test this code path");
	}

	assert(mp->fops.open != NULL);

	return mp->fops.open(mp->dev, relpath, mode);
}

int read(int fd, void *buf, int length) {
	assert(fd <= MAX_OPEN_FILES);

	struct open_file *file = (struct open_file *)&current_task->fdtable[fd];

	mountpoint_t *mp = file->mp;
	assert(mp != NULL);
	assert(mp->fops.read != NULL);

	return mp->fops.read(fd, buf, length);
}

int close(int fd) {
	assert(fd <= MAX_OPEN_FILES);

	struct open_file *file = (struct open_file *)&current_task->fdtable[fd];

	mountpoint_t *mp = file->mp;
	assert(mp != NULL);
	assert(mp->fops.close != NULL);

	return mp->fops.close(fd);
}
