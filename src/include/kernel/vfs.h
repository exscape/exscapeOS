#ifndef _FILEIO_H
#define _FILEIO_H

#include <sys/types.h>
#include <kernel/list.h>
#include <sys/stat.h>

#define	SEEK_SET 0
#define	SEEK_CUR 1
#define	SEEK_END 2

/* forward declarations */
struct fat32_partition;
struct mountpoint;
struct dir_ops;
struct dir;
struct task;

typedef struct dir_ops {
	struct dirent *(*readdir)(struct dir *);
	int (*closedir)(struct dir *);
} dir_ops_t;

typedef struct dir {
	uint32 dev; // the partition associated with this DIR
	uint32 ino;

	// These four are used by FAT...
	uint8 *buf;
	int _buflen; /* buffer's malloc'ed size */
	int pos;
	int len; /* number of valid data bytes in the buffer */

	// ... while this is used by ext2, which uses getdents to implements readdir.
	// Some of the above are still used, as getdents can return more that one entry at a time.
	int fd;

	struct mountpoint *mp;
	struct dir_ops dops;
} DIR;

typedef struct mp_ops {
	int (*open)(uint32 /* dev */, const char * /* absolute path */, int /* mode */);
	DIR *(*opendir)(struct mountpoint *, const char * /* absolute path */);
	int (*stat)(struct mountpoint *, const char * /* path */, struct stat *);
	int (*lstat)(struct mountpoint *, const char * /* path */, struct stat *);
	ssize_t (*readlink)(struct mountpoint *, const char *, char *, size_t);
} mp_ops_t;

struct open_file;
typedef struct open_file_ops {
	int   (*read)(int /* fd */, void * /* buf */, size_t /* length */);
	int   (*write)(int /* fd */, const void * /* buf */, size_t /* length */);
	int   (*close)(int /* fd */, struct open_file * /* file */);
	off_t (*lseek)(int /* fd */, off_t /* offset */, int /* whence */);
	int   (*fstat)(int /* fd */, struct stat *);
	int (*getdents)(int /* fd */, void * /* buffer */, int /* count */);
} open_file_ops_t;

typedef struct mountpoint {
	char path[1024];

	uint32 dev;
	struct mp_ops mpops;
	int depth; // 0 for a path of /, 1 for /mnt, 2 for /mnt/ext2 and so on.
} mountpoint_t;

/* A list of the mountpoints currently used */
extern list_t *mountpoints;

mountpoint_t *find_mountpoint_for_path(const char *path);

#include <sys/dirent.h>

#define MAX_OPEN_FILES 128

#define O_RDONLY 0
#define O_DIRECTORY 0x10000

#define MAX_DEVS 8
extern void *devtable[MAX_DEVS];
extern uint32 next_dev;

#define DEV_PIPE 0x7fff

typedef struct open_file {
	int count; // number of fds that link to this file
	dev_t dev;
	ino_t ino;
	ino_t cur_block; // current cluster number for FAT
	off_t offset;
	off_t size;
	int mode;
	mountpoint_t *mp;
	char *path;
	struct open_file_ops fops;
	void *data; // implementation specific data
} open_file_t;

struct open_file *get_filp(int fd);
struct open_file *new_filp(int *fd);
void destroy_filp(int fd);

struct open_file *do_get_filp(int fd, struct task *task);
int do_close(int fd, struct task *task);

// Resolves all symlinks in a given path.
int resolve_actual_path(char *out_path, size_t bufsize);

int open(const char *path, int mode);
int read(int fd, void *buf, int length);
int write(int fd, const void *buf, int length);
int close(int fd);
DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);
//int stat(const char *in_path, struct stat *buf);
//int fstat(int fd, struct stat *buf);
int chdir(const char *path);
off_t lseek(int fd, off_t offset, int whence);
int getdents (int fd, void *dp, int count);
//int lstat(const char *path, struct stat *buf);
ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);

int dup(int);
int dup2(int, int);

bool find_relpath(const char *in_path, char *relpath, mountpoint_t **mp_out);

#endif
