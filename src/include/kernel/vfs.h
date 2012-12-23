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

typedef struct dir_ops {
	struct dirent *(*readdir)(struct dir *);
	int (*closedir)(struct dir *);
} dir_ops_t;

typedef struct dir {
	uint32 dev; // the partition associated with this DIR
	uint32 ino;

	uint8 *buf;
	uint32 _buflen; /* buffer's malloc'ed size */
	uint32 pos;
	uint32 len; /* number of valid data bytes in the buffer */
	struct mountpoint *mp;
	struct dir_ops dops;
} DIR;

typedef struct mp_ops {
	int (*open)(uint32 /* dev */, const char * /* absolute path */, int /* mode */);
	DIR *(*opendir)(struct mountpoint *, const char * /* absolute path */);
	int (*stat)(struct mountpoint *, const char * /* path */, struct stat *);
} mp_ops_t;

typedef struct open_file_ops {
	int   (*read)(int /* fd */, void * /* buf */, size_t /* length */);
	int   (*write)(int /* fd */, const void * /* buf */, size_t /* length */);
	int   (*close)(int /* fd */);
	off_t (*lseek)(int /* fd */, off_t /* offset */, int /* whence */);
	int   (*fstat)(int /* fd */, struct stat *);
	int (*getdents)(int /* fd */, void * /* buffer */, int /* count */);
} open_file_ops_t;

typedef struct mountpoint {
	char path[1024];

	uint32 dev;
	struct mp_ops mpops;
} mountpoint_t;

/* A list of the mountpoints currently used */
extern list_t *mountpoints;

mountpoint_t *find_mountpoint_for_path(const char *path);

#include <sys/dirent.h>

#define MAX_OPEN_FILES 128

#define O_RDONLY 0

#define MAX_DEVS 8
extern void *devtable[MAX_DEVS];
extern uint32 next_dev;

typedef struct open_file {
	int count; // number of fds that link to this file; TODO
	dev_t dev;
	ino_t ino;
	ino_t _cur_ino; // current cluster number, i.e. when offset != 0
	off_t offset;
	off_t size;
	mountpoint_t *mp;
	char *path;
	struct open_file_ops fops;
	void *data; // implementation specific data
} open_file_t;

struct open_file *get_filp(int fd);
struct open_file *new_filp(int *fd);

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

bool find_relpath(const char *in_path, char *relpath, mountpoint_t **mp_out);

#define lstat stat

#endif
