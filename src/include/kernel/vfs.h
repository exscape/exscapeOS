#ifndef _FILEIO_H
#define _FILEIO_H

#include <types.h>
#include <kernel/list.h>

#define	SEEK_SET 0
#define	SEEK_CUR 1
#define	SEEK_END 2

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

// Flags used in stat.st_mode
#define S_IFMT   0170000	/* bit mask for the file type bit fields */
#define S_IFSOCK 0140000	/* socket */
#define S_IFLNK  0120000	/* symbolic link */
#define S_IFREG  0100000	/* regular file */
#define S_IFBLK  0060000	/* block device */
#define S_IFDIR  0040000	/* directory */
#define S_IFCHR  0020000	/* character device */
#define S_IFIFO  0010000	/* FIFO */
#define S_ISUID  0004000	/* set UID bit */
#define S_ISGID  0002000	/* set-group-ID bit (see below) */
#define S_ISVTX  0001000	/* sticky bit (see below) */
#define S_IRWXU  00700		/* mask for file owner permissions */
#define S_IRUSR  00400		/* owner has read permission */
#define S_IWUSR  00200		/* owner has write permission */
#define S_IXUSR  00100		/* owner has execute permission */
#define S_IRWXG  00070		/* mask for group permissions */
#define S_IRGRP  00040		/* group has read permission */
#define S_IWGRP  00020		/* group has write permission */
#define S_IXGRP  00010		/* group has execute permission */
#define S_IRWXO  00007		/* mask for permissions for others (not in group) */
#define S_IROTH  00004		/* others have read permission */
#define S_IWOTH  00002		/* others have write permission */
#define S_IXOTH  00001		/* others have execute permission */

#define S_ISREG(m) ((m & S_IFREG))
#define S_ISDIR(m) ((m & S_IFDIR))
#define S_ISCHR(m) ((m & S_IFCHR))
#define S_ISBLK(m) ((m & S_IFBLK))
#define S_ISFIFO(m) ((m & S_IFIFO))
#define S_ISLNK(m) ((m & S_IFLNK))
#define S_ISSOCK(m) ((m & S_IFSOCK))

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
} open_file_t;

int get_free_fd(void);

int open(const char *path, int mode);
int read(int fd, void *buf, int length);
int write(int fd, const void *buf, int length);
int close(int fd);
DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);
int stat(const char *in_path, struct stat *buf);
int chdir(const char *path);
off_t lseek(int fd, off_t offset, int whence);
int fstat(int fd, struct stat *buf);

bool find_relpath(const char *in_path, char *relpath, mountpoint_t **mp_out);

#define lstat stat

#endif
