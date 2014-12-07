#ifndef _INITRD_H
#define _INITRD_H

#include <sys/types.h>
#include <kernel/vfs.h>

typedef struct {
	uint32 nfiles;
} initrd_header_t;

/* Also defined in create_initrd.c */
typedef struct {
	int parent; /* inode of the parent directory; 1 for the root dir */
	int inode; /* inode of this file/directory; also 1 for the root dir */
	int mtime; /* unix timestamp */
	char name[64];
	uint32 mode; /* orig. file perms & ~0222 - includes S_ISDIR() etc. flags */
	uint32 offset; /* # of bytes into initrd file is located. 0 for directories */
	uint32 length; /* file: # bytes. dir: # direct child entries...? */
} initrd_file_header_t;

/* Creates the initrd; the argument is the memory location of the multiboot module */
void init_initrd(uint32 location);
bool fs_mount(void);

#endif
