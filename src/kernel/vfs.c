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

struct open_file *do_get_filp(int fd, task_t *task) {
	if (fd < 0 || fd > MAX_OPEN_FILES)
		return NULL;
	return (struct open_file *)task->fdtable[fd];
}

struct open_file *get_filp(int fd) {
	return do_get_filp(fd, (task_t *)current_task);
}

/*
int get_free_fd(void) {
	// TODO: optimize this
	struct open_file *f;
	for (int i = 0; i < MAX_OPEN_FILES; i++) {
		if (current_task->fdtable[i] == NULL) {
			return i;
		}
	}

	return -1;
}
*/

struct open_file *new_filp(int *fd) {
	assert(fd != NULL);
	size_t i = 0;
	do {
		if (current_task->fdtable[i] == NULL) {
			current_task->fdtable[i] = kmalloc(sizeof(struct open_file));
			memset(current_task->fdtable[i], 0, sizeof(struct open_file));
			*fd = i;
			return current_task->fdtable[i];
		}
	} while (++i <= MAX_OPEN_FILES);

	panic("new_filp: no free files! TODO: create more dynamically");
	return NULL;
}

void destroy_filp(int fd) {
	assert(fd >= 0);
	assert(fd < MAX_OPEN_FILES);
	assert(current_task->fdtable[fd] != NULL);

	kfree(current_task->fdtable[fd]);
	current_task->fdtable[fd] = NULL;
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

// Only used from within the kernel!
// Userspace has these in Newlib, which uses the getdents() system call.
DIR *opendir(const char *path) {
	assert(path != NULL);
	assert(*path != 0);

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

	if (mode != O_RDONLY && mode != (O_RDONLY | O_DIRECTORY))
		return -EACCES;

	assert(mp->mpops.open != NULL);
	return mp->mpops.open(mp->dev, relpath, mode);
}

int sys_open(const char *path, int mode) {
	if (!CHECK_ACCESS_STR(path))
		return -EFAULT;
	return open(path, mode);
}

int stat(const char *path, struct stat *buf) {
	if (path == NULL || buf == NULL)
		return -EFAULT;

	char relpath[PATH_MAX+1] = {0};
	mountpoint_t *mp = NULL;
	if (!find_relpath(path, relpath, &mp))
		return -1;

	assert(mp->mpops.stat != NULL);
	return mp->mpops.stat(mp, relpath, buf);
}

int sys_stat(const char *path, struct stat *buf) {
	if (!CHECK_ACCESS_STR(path) || !CHECK_ACCESS_WRITE(buf, sizeof(struct stat)))
		return -EFAULT;
	return stat(path, buf);
}

int read(int fd, void *buf, int length) {
	struct open_file *file = get_filp(fd);
	if (file == NULL)
		return -EBADF;

	if (file->count < 1)
		return -EBADF;

	if (file->fops.read == NULL)
		return -EBADF;
	return file->fops.read(fd, buf, length);
}

int sys_read(int fd, void *buf, int length) {
	if (!CHECK_ACCESS_WRITE(buf, length))
		return -EFAULT;
	return read(fd, buf, length);
}

int write(int fd, const void *buf, int length) {
	struct open_file *file = get_filp(fd);
	if (file == NULL)
		return -EBADF;

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

int sys_write(int fd, const void *buf, int length) {
	if (!CHECK_ACCESS_READ(buf, length))
		return -EFAULT;
	return write(fd, buf, length);
}

int fstat(int fd, struct stat *buf) {
	struct open_file *file = get_filp(fd);
	if (file == NULL)
		return -EBADF;

	if (file->count < 1)
		return -EBADF;

	if (file->fops.fstat == NULL)
		panic("fstat on fd %d: fops.fstat == NULL", fd);

	return file->fops.fstat(fd, buf);
}

int sys_fstat(int fd, struct stat *buf) {
	if (!CHECK_ACCESS_WRITE(buf, sizeof(struct stat)))
		return -EFAULT;
	return fstat(fd, buf);
}

int getdents(int fd, void *dp, int count) {
	struct open_file *file = get_filp(fd);
	if (file == NULL)
		return -EBADF;

	if (file->count < 1)
		return -EBADF;

	if(file->fops.getdents == NULL)
		return -ENOTDIR;
	return file->fops.getdents(fd, dp, count);
}

int sys_getdents(int fd, void *dp, int count) {
	if (!CHECK_ACCESS_WRITE(dp, count))
		return -EFAULT;
	return getdents(fd, dp, count);
}

int do_close(int fd, task_t *task) {
	assert(task != NULL);

	struct open_file *file = do_get_filp(fd, task);
	if (file == NULL)
		return -EBADF;

	if (file->count < 1)
		return -EBADF;

	assert(file->fops.close != NULL);

	int r = file->fops.close(fd, file);
	file->count--;
	task->fdtable[fd] = NULL;
	assert(file->count >= 0);

	if (file->count == 0) {
		if (file->path != NULL)
			kfree(file->path);
		memset(file, 0, sizeof(struct open_file));
		kfree(file);
	}

	return r;
}

int close(int fd) {
	return do_close(fd, (task_t *)current_task);
}

// Only used from within the kernel!
// Userspace has these in Newlib, which uses the getdents() system call.
int closedir(DIR *dir) {
	assert(dir != NULL);
	assert(dir->dops.closedir != NULL);
	return dir->dops.closedir(dir);
}

// Only used from within the kernel!
// Userspace has these in Newlib, which uses the getdents() system call.
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
		// Relative path
		strlcpy(path, current_task->pwd, PATH_MAX+1);
		path_join(path, in_path);
	}

	int err = 0;

	char *old_pwd = kmalloc(strlen(current_task->pwd) + 1);
	strcpy(old_pwd, current_task->pwd);

	strlcpy(current_task->pwd, path, PATH_MAX + 1);

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
	strlcpy(current_task->pwd, old_pwd, PATH_MAX+1);
	kfree(old_pwd);
	return err;
}

char *sys_getcwd(char *buf, size_t size) {
	if (buf == NULL || size == 0) {
		// Newlib should never call with these arguments
		panic("sys_getcwd(): buf == NULL || size == 0");
		return (char *)(-EFAULT);
	}
	if (!CHECK_ACCESS_WRITE(buf, size)) {
		// Ditto
		panic("sys_getcwd(): passed invalid buffer");
		return (char *)(-EFAULT);
	}

	assert(current_task->pwd != NULL);

	if (strlen(current_task->pwd) >= size) {
		return (char *)(-ERANGE);
	}

	strlcpy(buf, current_task->pwd, size);

	return buf;
}

int sys_chdir(const char *in_path) {
	if (!CHECK_ACCESS_STR(in_path))
		return -EFAULT;
	return chdir(in_path);
}

off_t lseek(int fd, off_t offset, int whence) {
	struct open_file *file = get_filp(fd);
	if (file == NULL || file->fops.lseek == NULL)
		return -EBADF;

	if (file->dev == DEV_PIPE)
		return -ESPIPE;

	return file->fops.lseek(fd, offset, whence);
}

int dup(int fd) {
	// Duplicate a file descriptor. That is, given an fd,
	// return another fd that points to the same file.

	INTERRUPT_LOCK;
	struct open_file *file = get_filp(fd);
	if (file == NULL) {
		INTERRUPT_UNLOCK;
		return -EBADF;
	}

	// Find the lowest-numbered free file descriptor
	for (int i = 0; i < MAX_OPEN_FILES; i++) {
		if (current_task->fdtable[i] == NULL) {
			// Found it!
			current_task->fdtable[i] = file;
			file->count++;
			INTERRUPT_UNLOCK;
			return i;
		}
	}

	// We found no free fds!
	INTERRUPT_UNLOCK;
	return -EMFILE;
}

int dup2(int fd, int fd2) {
	if (fd2 < 0 || fd2 >= MAX_OPEN_FILES) {
		/* fd is checked in get_filp */
		return -EBADF;
	}

	INTERRUPT_LOCK;
	struct open_file *file = get_filp(fd);
	if (file == NULL) {
		INTERRUPT_UNLOCK;
		return -EBADF;
	}

	struct open_file *tmp = get_filp(fd2);
	if (tmp != NULL) {
		// We need to close this open file first!
		close(fd2);
	}

	// Now then: set fd2 to point at the file from fd
	assert(current_task->fdtable[fd2] == NULL);
	current_task->fdtable[fd2] = file;
	file->count++;

	INTERRUPT_UNLOCK;
	return fd2;
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

	list_foreach(mountpoints, it) {
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
