#ifndef _VFS_H
#define _VFS_H

#include <types.h>
#include <kernel/list.h>
//#include <kernel/fat.h>

/* Forward declaration, since there's a catch 22 that the function prototypes and the struct need each other */
struct fs_node; 

/* Function declarations for the VFS (for sensible parameter names, look further below) */
typedef uint32 (*read_type_t)(struct fs_node *, uint32, uint32, uint8 *);
typedef uint32 (*write_type_t)(struct fs_node *, uint32, uint32, uint8 *);
typedef uint32 (*fsize_type_t)(struct fs_node *);
typedef void (*open_type_t)(struct fs_node *);
typedef void (*close_type_t)(struct fs_node *);
typedef struct dirent * (*readdir_type_t)(struct fs_node *, uint32);
typedef struct fs_node * (*finddir_type_t)(struct fs_node *, const char *name);

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

typedef struct file_ops {
	int (*open)(uint32 /* dev */, const char * /* absolute path */, int /* mode */);
	int (*read)(int /* fd */, void * /* buf */, size_t /* length */);
	int (*close)(int /* fd */);
	DIR *(*opendir)(struct mountpoint *, const char * /* absolute path */);
	struct dirent *(*readdir)(DIR *);
	int (*closedir)(DIR *);
} file_ops_t;

typedef struct mountpoint {
	char path[1024];

	uint32 dev;
	struct file_ops fops;
} mountpoint_t;


/* A list of the mountpoints currently used */
extern list_t *mountpoints;

mountpoint_t *find_mountpoint_for_path(const char *path);

/* A struct describing a node (file, directory, ...) */
typedef struct fs_node {
	char name[128];
	uint32 perm;   /* permissions mask */
	uint32 uid;
	uint32 gid;
	uint32 flags;  /* See the FS_* defines below */
	uint32 inode;  /* inode number */
	uint32 length; /* file size (in bytes) */
	uint32 impl; /* currently unused */

	/* Pointers to various functions a node may require */
	read_type_t    read;
	write_type_t   write;
	open_type_t    open;
	close_type_t   close;
	readdir_type_t readdir;
	finddir_type_t finddir;
	fsize_type_t   fsize;

	/* Used by mountpoints and symlinks */
	struct fs_node *ptr;
} fs_node_t;

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

/* The set of standard VFS functions */
uint32 read_fs(fs_node_t *node, uint32 offset, uint32 size, uint8 *buffer);
uint32 write_fs(fs_node_t *node, uint32 offset, uint32 size, uint8 *buffer);
void open_fs(fs_node_t *node, uint8 read, uint8 write);
void close_fs(fs_node_t *node);
struct dirent *readdir_fs(fs_node_t *node, uint32 index);
fs_node_t *finddir_fs(fs_node_t *node, const char *name);
uint32 fsize(fs_node_t *node);

#endif /* header guard */
