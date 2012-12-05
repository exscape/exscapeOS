#include <kernel/fileio.h>
#include <kernel/vfs.h>
#include <kernel/fat.h>
#include <string.h>
#include <path.h>
#include <kernel/task.h>

#include <kernel/kernutil.h> /* panic */

// Stores FS-specific data, indexed by device number
// Cast to the correct pointer as needed
void *devtable[MAX_DEVS] = {0};
uint32 next_dev = 0;

DIR *opendir(const char *path) {
	assert(path != NULL);

	if (path[0] != '/')
		return NULL; // only absolute paths are supported

	char relpath[1024] = {0};
	mountpoint_t *mp = find_mountpoint_for_path(path);
	assert(mp != NULL);

	assert(strnicmp(path, mp->path, strlen(mp->path)) == 0); // First part of the part should be the mountpoint path

	if (strcmp(mp->path, "/") == 0)
		strlcpy(relpath, path, 1024);
	else {
		// Strip the mountpoint from the beginning
		strlcpy(relpath, path + strlen(mp->path), 1024);
		if (relpath[0] == 0)
			strcpy(relpath, "/");
	}

	assert(mp->fops.opendir != NULL);

	return mp->fops.opendir(mp, relpath);
}

struct dirent *readdir(DIR *dir) {
	assert(dir != NULL);
	assert(dir->mp != NULL);
	return dir->mp->fops.readdir(dir);
}

int closedir(DIR *dir) {
	assert(dir != NULL);
	assert(dir->mp != NULL);
	return dir->mp->fops.closedir(dir);
}

int open(const char *path, int mode) {
	assert(mode == O_RDONLY);
	assert(path != NULL);

	if (path[0] != '/')
		return -1; // only absolute paths are supported

	mountpoint_t *mp = find_mountpoint_for_path(path);
	assert(mp != NULL);

	char relpath[1024] = {0};

	assert(strnicmp(path, mp->path, strlen(mp->path)) == 0); // First part of the part should be the mountpoint path

	if (strcmp(mp->path, "/") == 0)
		strlcpy(relpath, path, 1024);
	else {
		// Strip the mountpoint from the beginning
		strlcpy(relpath, path + strlen(mp->path), 1024);
		if (relpath[0] == 0)
			strcpy(relpath, "/");
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

int stat(const char *path, struct stat *buf) {
	assert(path != NULL);
	assert(buf != NULL);

	if (path[0] != '/')
		return -1; // only absolute paths are supported

	mountpoint_t *mp = find_mountpoint_for_path(path);
	assert(mp != NULL);

	char relpath[1024] = {0};

	assert(strnicmp(path, mp->path, strlen(mp->path)) == 0); // First part of the part should be the mountpoint path

	if (strcmp(mp->path, "/") == 0)
		strlcpy(relpath, path, 1024);
	else {
		// Strip the mountpoint from the beginning
		strlcpy(relpath, path + strlen(mp->path), 1024);
		if (relpath[0] == 0)
			strcpy(relpath, "/");
	}

	assert(mp->fops.stat != NULL);

	return mp->fops.stat(mp, relpath, buf);
}

int chdir(const char *path) {
	// TODO: real chdir()!
	if (current_task->pwd)
		kfree(current_task->pwd);

	current_task->pwd = strdup(path);

	return 0;
}
