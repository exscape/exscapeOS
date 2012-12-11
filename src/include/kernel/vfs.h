#ifndef _FILEIO_H
#define _FILEIO_H

#include <types.h>
#include <kernel/list.h>

// TODO: this REALLY doesn't belong here
typedef uint16 dev_t;
typedef uint16 mode_t;
typedef uint16 nlink_t;
typedef uint16 uid_t;
typedef uint16 gid_t;
typedef sint64 off_t;
typedef sint32 blkcnt_t;
typedef sint32 blksize_t;
typedef uint32 ino_t;
typedef sint32 time_t;

// TODO: this doesn't really belong here
struct stat {
	dev_t	st_dev;
	ino_t	st_ino;
	mode_t	st_mode;
	nlink_t	st_nlink;
	uid_t	st_uid;
	gid_t	st_gid;
	dev_t	st_rdev;
	off_t	st_size;
	time_t	st_atime;
	time_t	st_mtime;
	time_t	st_ctime;
	blksize_t st_blksize;
	blkcnt_t st_blocks;
};

/* forward declarations */
struct fat32_partition;
struct mountpoint;

typedef struct dir {
	uint32 dev; // the partition associated with this DIR
	uint32 ino;

	uint8 *buf;
	uint32 _buflen; /* buffer's malloc'ed size */
	uint32 pos;
	uint32 len; /* number of valid data bytes in the buffer */
	struct mountpoint *mp;
} DIR;

typedef struct mp_ops {
	int (*open)(uint32 /* dev */, const char * /* absolute path */, int /* mode */);
	DIR *(*opendir)(struct mountpoint *, const char * /* absolute path */);
	struct dirent *(*readdir)(DIR *);
	int (*closedir)(DIR *);
	int (*stat)(struct mountpoint *, const char * /* path */, struct stat *);
} mp_ops_t;

typedef struct open_file_ops {
	int (*read)(int /* fd */, void * /* buf */, size_t /* length */);
	int (*write)(int /* fd */, void * /* buf */, size_t /* length */);
	int (*close)(int /* fd */);
	/* TODO: fstat */
} open_file_ops_t;

typedef struct mountpoint {
	char path[1024];

	uint32 dev;
	struct mp_ops mpops;
} mountpoint_t;

/* A list of the mountpoints currently used */
extern list_t *mountpoints;

mountpoint_t *find_mountpoint_for_path(const char *path);

#define DIRENT_NAME_LEN 256

/* POSIX struct dirent */
struct dirent {
	uint32 d_ino;
	uint16 d_dev; // custom field, to allow a dirent to uniquely specify a file
	uint16 __pad;
	uint16 d_reclen;
	uint8 d_type;
	uint8 d_namlen;
	char d_name[DIRENT_NAME_LEN];
};

// Used by glibc, perhaps others? They may be useful some day
#define _DIRENT_HAVE_D_NAMLEN
#define _DIRENT_HAVE_D_RECLEN
#define _DIRENT_HAVE_D_TYPE

// struct dirent.flags
enum {
	DT_UNKNOWN = 0,
	DT_FIFO = 1,
	DT_CHR = 2,
	DT_DIR = 4,
	DT_BLK = 6,
	DT_REG = 8,
	DT_LNK = 10,
	DT_SOCK = 12,
    DT_WHT = 14
};

/* Flags used in struct fs_node */
/* All except FS_MOUNTPOINT are mutually exclusive */
#define FS_FILE   0x01
#define FS_DIRECTORY 0x02
#define FS_CHARDEV 0x03
#define FS_BLOCKDEV 0x04
#define FS_PIPE 0x05
#define FS_SYMLINK 0x06
#define FS_MOUNTPOINT 0x08

#define MAX_OPEN_FILES 128

#define O_RDONLY 0

#define MAX_DEVS 8
extern void *devtable[MAX_DEVS];
extern uint32 next_dev;

typedef struct open_file {
	dev_t dev;
	ino_t ino;
	ino_t _cur_ino; // current cluster number, i.e. when offset != 0
	off_t offset;
	off_t size;
	mountpoint_t *mp;
	char *path;
	struct open_file_ops fops;
} open_file_t;

int open(const char *path, int mode);
int read(int fd, void *buf, int length);
int write(int fd, void *buf, int length);
int close(int fd);
DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);
int stat(const char *in_path, struct stat *buf);
int chdir(const char *path);
#define lstat stat

#endif
