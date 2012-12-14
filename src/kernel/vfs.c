#include <kernel/vfs.h>
#include <kernel/vfs.h>
#include <kernel/fat.h>
#include <string.h>
#include <path.h>
#include <kernel/task.h>
#include <kernel/kernutil.h>
#include <sys/errno.h>

// Stores FS-specific data, indexed by device number
// Cast to the correct pointer as needed
void *devtable[MAX_DEVS] = {0};
uint32 next_dev = 0;

list_t *mountpoints = NULL;

int get_free_fd(void) {
	// TODO: optimize this
	for (int i = 0; i < MAX_OPEN_FILES; i++) {
		if (current_task->fdtable[i].count == 0)
			return i;
	}

	return -1;
}

bool find_relpath(const char *in_path, char *relpath, mountpoint_t **mp_out) {
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

	if (mp_out != NULL)
		*mp_out = mp;

	return true;
}

DIR *opendir(const char *path) {
	char relpath[PATH_MAX+1] = {0};
	mountpoint_t *mp = NULL;
	if (!find_relpath(path, relpath, &mp))
		return NULL;

	assert(mp->mpops.opendir != NULL);
	return mp->mpops.opendir(mp, relpath);
}

int open(const char *path, int mode) {
	char relpath[PATH_MAX+1] = {0};
	mountpoint_t *mp = NULL;
	if (!find_relpath(path, relpath, &mp))
		return -1;

	if (mode != O_RDONLY)
		return -EACCES;

	assert(mp->mpops.open != NULL);
	return mp->mpops.open(mp->dev, relpath, mode);
}

int stat(const char *path, struct stat *buf) {
	assert(buf != NULL);

	char relpath[PATH_MAX+1] = {0};
	mountpoint_t *mp = NULL;
	if (!find_relpath(path, relpath, &mp))
		return -1;

	assert(mp->mpops.stat != NULL);
	return mp->mpops.stat(mp, relpath, buf);
}

int read(int fd, void *buf, int length) {
	if (fd < 0 || fd >= MAX_OPEN_FILES)
		return -EBADF;

	struct open_file *file = (struct open_file *)&current_task->fdtable[fd];
	if (file->count < 1)
		return -EBADF;

	if (file->fops.read == NULL)
		return -EBADF;
	return file->fops.read(fd, buf, length);
}

int write(int fd, const void *buf, int length) {
	if (fd < 0 || fd >= MAX_OPEN_FILES)
		return -EBADF;

	struct open_file *file = (struct open_file *)&current_task->fdtable[fd];
	if (file->count < 1)
		return -EBADF;

	if (file->fops.write == NULL) {
		if (file->path && file->path[0]) {
			// No FS supports writing yet; return EPERM for these
			return -EPERM;
		}
		else {
			// Not sure what to return here, though it should never happen at the moment
			return -EBADF;
		}
	}
	return file->fops.write(fd, buf, length);
}

int fstat(int fd, struct stat *buf) {
	if (fd < 0 || fd >= MAX_OPEN_FILES)
		return -EBADF;

	struct open_file *file = (struct open_file *)&current_task->fdtable[fd];
	if (file->count < 1)
		return -EBADF;

	if (file->fops.fstat == NULL)
		panic("fstat on fd %d: fops.fstat == NULL", fd);

	return file->fops.fstat(fd, buf);
}

int close(int fd) {
	if (fd < 0 || fd >= MAX_OPEN_FILES)
		return -EBADF;

	struct open_file *file = (struct open_file *)&current_task->fdtable[fd];
	if (file->count < 1)
		return -EBADF;

	assert(file->fops.close != NULL);

	int r = file->fops.close(fd);
	memset(file, 0, sizeof(struct open_file));

	return r;
}

int closedir(DIR *dir) {
	assert(dir != NULL);
	assert(dir->dops.closedir != NULL);
	return dir->dops.closedir(dir);
}

struct dirent *readdir(DIR *dir) {
	assert(dir != NULL);
	assert(dir->dops.readdir != NULL);
	return dir->dops.readdir(dir);
}

int chdir(const char *in_path) {
	assert(in_path != NULL);
	assert(current_task->pwd != NULL);

	char path[PATH_MAX+1] = {0};
	if (in_path[0] == '/') {
		// Absolute path
		strlcpy(path, in_path, PATH_MAX+1);
	}
	else {
		// Relatve path
		strlcpy(path, current_task->pwd, PATH_MAX+1);
		path_join(path, in_path);
	}

	int err = 0;

	char *old_pwd = current_task->pwd;
	size_t path_len = strlen(path);
	current_task->pwd = kmalloc(path_len + 1);
	strlcpy(current_task->pwd, path, path_len + 1);

	// Now that we can modify "path" freely, let's validate it (yes, after setting it!)
	char *tmp;
	char *token = NULL;
	char verified[PATH_MAX+1] = {0}; // starts as /, and builds up to the full path (if it's OK)
	strcpy(verified, "/");
	struct dirent *dent = NULL;
	size_t len = 0;
	for (token = strtok_r(path, "/", &tmp); token != NULL; token = strtok_r(NULL, "/", &tmp)) {
		len = strlen(token);
		DIR *dir = opendir(verified);
		if (!dir) {
			err = -ENOENT; // TODO: probably?
			goto error;
		}
		while ((dent = readdir(dir)) != NULL) {
			if (len == dent->d_namlen && stricmp(dent->d_name, token) == 0) {
				// This is the token
				if (dent->d_type != DT_DIR) {
					closedir(dir);
					err = -ENOTDIR;
					goto error;
				}
				path_join(verified, token);
				break;
			}
		}
		closedir(dir);
	}

	if (stricmp(verified, current_task->pwd) != 0) {
		err = -ENOENT;
		goto error;
	}

	kfree(old_pwd);
	return 0;

error:
	current_task->pwd = old_pwd;
	return err;

}

off_t lseek(int fd, off_t offset, int whence) {
	if (fd < 0 || fd >= MAX_OPEN_FILES)
		return -EBADF;

	struct open_file *file = (struct open_file *)&current_task->fdtable[fd];

	if (file->fops.lseek == NULL)
		return -EBADF;
	return file->fops.lseek(fd, offset, whence);
}

mountpoint_t *find_mountpoint_for_path(const char *path) {
	if (mountpoints == NULL || mountpoints->count == 0 || path == NULL)
		return NULL;

	/*
	 * Simple but ugly: find the mountpoint which matches as much as possible
	 * of the path. For example, the mountpoint "/data/x/y" matches more than
	 * just "/data" of the path "/data/x/y/file.txt", so the longer one is used.
	 */

	mountpoint_t *best_match = NULL;
	size_t best_match_len = 0;

	for (node_t *it = mountpoints->head; it != NULL; it = it->next) {
		mountpoint_t *mp = (mountpoint_t *)it->data;
		size_t len = strlen(mp->path);
		if (strlen(path) < len)
			continue;

		if (strnicmp(mp->path, path, len) == 0) {
			if (len > 1 && path[len] != 0 && path[len] != '/') {
				continue; // TODO: TEST this!
			}

			// First part matches!
			//if (stricmp(mp->path, path) == 0 && len != 1 /* not the / mountpoint */) {
				// If they are EXACTLY equal, then we actually DON'T want this mountpoint!
				// For example: stat("/initrd", &st);
				// We should NOT stat the root directory of the initrd filesystem,
				// but rather the "initrd" directory in the / filesystem!
				//continue;
				//}
			if (len > best_match_len) {
				best_match = mp;
				best_match_len = len;
			}
		}
	}

	return best_match;
}
