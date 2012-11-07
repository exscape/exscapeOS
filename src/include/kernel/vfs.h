#ifndef _VFS_H
#define _VFS_H

#include <types.h>
#include <kernel/list.h>
#include <kernel/fat.h>

/* Forward declaration, since there's a catch 22 that the function prototypes and the struct need each other */
struct fs_node; 

/* Function declarations for the VFS (for sensible parameter names, look further below) */
typedef uint32 (*read_type_t)(struct fs_node *, uint32, uint32, uint8 *);
typedef uint32 (*write_type_t)(struct fs_node *, uint32, uint32, uint8 *);
typedef void (*open_type_t)(struct fs_node *);
typedef void (*close_type_t)(struct fs_node *);
typedef struct dirent * (*readdir_type_t)(struct fs_node *, uint32);
typedef struct fs_node * (*finddir_type_t)(struct fs_node *, const char *name);

/* forward declaration */
struct fat32_partition;

typedef struct mountpoint {
	char path[512];

	/* Will be changed to a FS-neutral type when needed */
	struct fat32_partition *partition;
} mountpoint_t;

typedef struct dir {
	/* TODO: proper per-process file descriptors */
	struct fat32_partition *partition;
	uint32 dir_cluster;

	list_t *entries; /* used by readdir() */
	node_t *ptr;
	uint32 len; /* how many entries there are */
} DIR;

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

	/* Used by mountpoints and symlinks */
	struct fs_node *ptr;
} fs_node_t;

/* The root of the filesystem hierarchy */
extern fs_node_t *fs_root;

/* POSIX struct dirent */
struct dirent {
	char d_name[256];
	uint32 d_ino;
	bool is_dir;
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

/* The set of standard VFS functions */
uint32 read_fs(fs_node_t *node, uint32 offset, uint32 size, uint8 *buffer);
uint32 write_fs(fs_node_t *node, uint32 offset, uint32 size, uint8 *buffer);
void open_fs(fs_node_t *node, uint8 read, uint8 write);
void close_fs(fs_node_t *node);
struct dirent *readdir_fs(fs_node_t *node, uint32 index);
fs_node_t *finddir_fs(fs_node_t *node, const char *name);

#endif /* header guard */
