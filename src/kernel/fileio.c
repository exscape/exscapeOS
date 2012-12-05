#include <kernel/fileio.h>
#include <kernel/vfs.h>
#include <kernel/fat.h>
#include <string.h>
#include <path.h>
#include <kernel/task.h>
#include <kernel/kernutil.h>

// Stores FS-specific data, indexed by device number
// Cast to the correct pointer as needed
void *devtable[MAX_DEVS] = {0};
uint32 next_dev = 0;

static bool find_relpath(const char *in_path, char *relpath, mountpoint_t **mp_out) {
	// Transforms the path to a function (currently open, opendir and stat)
	// into a path relative to the mountpoint. E.g. /initrd/mounts would turn in to
	// just "/mounts" if the initrd is mounted under /initrd, and the initrd would then
	// open /mounts seen from the "initrd root".

	char path[PATH_MAX+1] = {0};

	assert(in_path != NULL);
	if (in_path[0] != '/') {
		// This is a relative path (which is unrelated to the name of this function, ugh),
		// e.g. "file.ext", "./file.ext" or "dir/file.ext"
		// Use $PWD to construct an absolute path, which we need below.
		if (current_task->pwd)
			strlcpy(path, current_task->pwd, PATH_MAX+1);
		else
			strcpy(path, "/");

		path_join(path, in_path);
	}
	else
		strlcpy(path, in_path, PATH_MAX+1);

	mountpoint_t *mp = find_mountpoint_for_path(path);
	assert(mp != NULL);

	assert(strnicmp(path, mp->path, strlen(mp->path)) == 0); // First part of the part should be the mountpoint path

	if (strcmp(mp->path, "/") == 0)
		strlcpy(relpath, path, PATH_MAX+1);
	else {
		// Strip the mountpoint from the beginning
		strlcpy(relpath, path + strlen(mp->path), PATH_MAX+1);
		if (relpath[0] == 0)
			strcpy(relpath, "/");
	}

	*mp_out = mp;
	return true;
}

DIR *opendir(const char *path) {
	char relpath[PATH_MAX+1] = {0};
	mountpoint_t *mp = NULL;
	if (!find_relpath(path, relpath, &mp))
		return NULL;

	assert(mp->fops.opendir != NULL);
	return mp->fops.opendir(mp, relpath);
}

int open(const char *path, int mode) {
	char relpath[PATH_MAX+1] = {0};
	mountpoint_t *mp = NULL;
	if (!find_relpath(path, relpath, &mp))
		return -1;

	assert(mode == O_RDONLY);
	assert(mp->fops.open != NULL);
	return mp->fops.open(mp->dev, relpath, mode);
}

int stat(const char *path, struct stat *buf) {
	assert(buf != NULL);

	char relpath[PATH_MAX+1] = {0};
	mountpoint_t *mp = NULL;
	if (!find_relpath(path, relpath, &mp))
		return -1;

	assert(mp->fops.stat != NULL);
	return mp->fops.stat(mp, relpath, buf);
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

int closedir(DIR *dir) {
	assert(dir != NULL);
	assert(dir->mp != NULL);
	return dir->mp->fops.closedir(dir);
}

struct dirent *readdir(DIR *dir) {
	assert(dir != NULL);
	assert(dir->mp != NULL);
	return dir->mp->fops.readdir(dir);
}

int chdir(const char *path) {
	// TODO: real chdir()!
	if (current_task->pwd)
		kfree(current_task->pwd);

	current_task->pwd = strdup(path);

	return 0;
}
