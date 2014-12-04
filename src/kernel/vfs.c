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

ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);

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

// Resolve any symbolic links, and return the "true" path into the buffer.
// Paths must be absolute.
bool resolve_actual_path(char *out_path, size_t bufsize) {
	assert(out_path != NULL);
	assert(bufsize >= strlen(out_path) + 1);
	assert(*out_path == '/');

	char path_buf[PATH_MAX+1] = {0};
	strlcpy(path_buf, out_path, PATH_MAX+1);
	char link_buf[PATH_MAX+1];
	char verified[PATH_MAX+1] = {0};
	strcpy(verified, "/");
	strcpy(out_path, "/");

	char *tmp;
	int ret;
	char *token = NULL;
	for (token = strtok_r(path_buf, "/", &tmp); token != NULL; (token = strtok_r(NULL, "/", &tmp))) {
		path_join(verified, token);

		memset(link_buf, 0, PATH_MAX+1);
		ret = readlink(verified, link_buf, PATH_MAX);
		if (ret == -EINVAL) {
			// The most likely cause for this errno is that this token wasn't a symlink, which means we should
			// append it to the output address.
			path_join(out_path, token);
		}
		else if (ret >= 0) {
			// This was a valid link, and link_buf now contains its contents.
			path_join(verified, "..");

			if (*link_buf == '/')
				strlcpy(verified, link_buf, PATH_MAX+1);
			else
				path_join(verified, link_buf);
		}
		else
			panic("Unhandled error in resolve_actual_path; could include e.g. -ENOENT! Return value: %d\n", ret);
	}

	strlcpy(out_path, verified, bufsize);

	return true;
}

bool find_relpath(const char *in_path, char *relpath, mountpoint_t **mp_out) {
	// Transforms the path to a function into a path relative to the mountpoint.
	// E.g. /initrd/mounts would turn in to just "/mounts" if the initrd is mounted under /initrd,
	// and the initrd would then open /mounts seen from the "initrd root".

	char *path = kmalloc(PATH_MAX+1);
	memset(path, 0, PATH_MAX+1);

	assert(in_path != NULL);
	if (in_path[0] != '/') {
		// This is a relative path (which is unrelated to the name of this function, ugh),
		// e.g. "file.ext", "../file.ext" or "dir/file.ext"
		// Use $PWD to construct an absolute path, which we need below.
		if (current_task->pwd)
			strlcpy(path, current_task->pwd, PATH_MAX+1);
		else
			strcpy(path, "/");

		// In case there are symlinks in $PWD, the mountpoint may be misleading.
		// For example, we could have a path like PWD = /ext2/somelink, where somelink points to ../somedir.
		// In that case, the "true" PWD is on the / mountpoint, *not* the ext2 one.
		// Note that this will call find_relpath again, however, that time on an absolute path, so this
		// code path (in_path[0] != '/) won't run, and there should be no risk of an infinite recursion.
		resolve_actual_path(path, PATH_MAX+1);

		path_join(path, in_path);
	}
	else
		strlcpy(path, in_path, PATH_MAX+1);

	// TODO(?): This does not act exactly as other OSes (e.g. Linux) do.
	// For example, if one does "cd somerandomstring/..", this code will simply act as
	// "cd .", while e.g. Linux will give you -ENOENT.
	if (!path_collapse_dots(path))
		panic("path_collapse_dots(\"%s\") failed!", path);

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

	kfree(path);
	return true;
}

// Only used from within the kernel!
// Userspace has these in Newlib, which uses the getdents() system call.
DIR *opendir(const char *path) {
	assert(path != NULL);
	assert(*path != 0);

	char *relpath = kmalloc(PATH_MAX+1);
	memset(relpath, 0, PATH_MAX+1);
	mountpoint_t *mp = NULL;
	if (!find_relpath(path, relpath, &mp)) {
		kfree(relpath);
		return NULL;
	}

	assert(mp->mpops.opendir != NULL);
	DIR *ret = mp->mpops.opendir(mp, relpath);
	kfree(relpath);
	return ret;
}

int open(const char *path, int mode) {
	char *relpath = kmalloc(PATH_MAX+1);
	memset(relpath, 0, PATH_MAX+1);
	mountpoint_t *mp = NULL;
	if (!find_relpath(path, relpath, &mp))
		return -1;

	if (mode != O_RDONLY && mode != (O_RDONLY | O_DIRECTORY))
		return -EACCES;

	assert(mp->mpops.open != NULL);
	int ret = mp->mpops.open(mp->dev, relpath, mode);
	kfree(relpath);
	return ret;
}

int sys_open(const char *path, int mode) {
	assert(current_task->link_count == 0);

	if (!CHECK_ACCESS_STR(path))
		return -EFAULT;
	return open(path, mode);
}

int stat(const char *path, struct stat *buf) {
	if (path == NULL || buf == NULL)
		return -EFAULT;

	char *relpath = kmalloc(PATH_MAX+1);
	memset(relpath, 0, PATH_MAX+1);
	mountpoint_t *mp = NULL;
	if (!find_relpath(path, relpath, &mp)) {
		kfree(relpath);
		return -1;
	}

	assert(mp->mpops.stat != NULL);
	int ret = mp->mpops.stat(mp, relpath, buf);
	kfree(relpath);
	return ret;
}

int sys_stat(const char *path, struct stat *buf) {
	assert(current_task->link_count == 0);

	if (!CHECK_ACCESS_STR(path) || !CHECK_ACCESS_WRITE(buf, sizeof(struct stat)))
		return -EFAULT;
	return stat(path, buf);
}

int lstat(const char *path, struct stat *buf) {
	if (path == NULL || buf == NULL)
		return -EFAULT;

	char *relpath = kmalloc(PATH_MAX+1);
	memset(relpath, 0, PATH_MAX+1);
	mountpoint_t *mp = NULL;
	if (!find_relpath(path, relpath, &mp)) {
		kfree(relpath);
		return -1;
	}

	int ret = -1;
	if (mp->mpops.lstat != NULL)
		ret = mp->mpops.lstat(mp, relpath, buf);
	else if (mp->mpops.stat != NULL) {
		// TODO: untested workaround/hack: there's no lstat support for FAT and initrd
		ret = mp->mpops.stat(mp, relpath, buf);
	}
	else
		panic("lstat and stat not supported");

	kfree(relpath);
	return ret;
}

int sys_lstat(const char *path, struct stat *buf) {
	assert(current_task->link_count == 0);

	if (!CHECK_ACCESS_STR(path) || !CHECK_ACCESS_WRITE(buf, sizeof(struct stat)))
		return -EFAULT;
	return lstat(path, buf);
}

ssize_t readlink(const char *pathname, char *buf, size_t bufsiz) {
	if (pathname == NULL || buf == NULL)
		return -EFAULT;
	if (bufsiz == 0)
		return -EINVAL;

	char *relpath = kmalloc(PATH_MAX+1);
	memset(relpath, 0, PATH_MAX+1);
	mountpoint_t *mp = NULL;
	if (!find_relpath(pathname, relpath, &mp)) {
		kfree(relpath);
		return -1;
	}

	ssize_t ret = -1;
	if (mp->mpops.readlink != NULL)
		ret = mp->mpops.readlink(mp, relpath, buf, bufsiz);
	else {
		// I'm not sure how correct this is. readlink on nonexisting paths will
		// obviously return EINVAL with this; I don't know whether they should return
		// ENOENT instead. However, since this FS (as of this writing initrd and FAT)
		// has no symlink support, EINVAL seems reasonable... but Linux returns ENOENT.
		ret = -EINVAL;
	}

	kfree(relpath);
	return ret;
}

ssize_t sys_readlink(const char *pathname, char *buf, size_t bufsiz) {
	assert(current_task->link_count == 0);

	if (!CHECK_ACCESS_STR(pathname) || !CHECK_ACCESS_WRITE(buf, bufsiz))
		return -EFAULT;
	return readlink(pathname, buf, bufsiz);
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

// Checks that a path exists and that it can be accessed.
// Returns 0 on success, -errno on failure.
int validate_path(const char *in_path) {
	assert(in_path != NULL);
	struct stat st;

	int err = stat(in_path, &st);
	if (err < 0)
		return err;

	if (S_ISDIR(st.st_mode))
		return 0;
	else
		return -ENOTDIR;
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

	int err;
	if ((err = validate_path(path)) == 0) {
		strlcpy(current_task->pwd, path, PATH_MAX + 1);
		return 0;
	}
	else
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
