#ifndef _INITRD_H
#define _INITRD_H

#include <types.h>
#include <kernel/vfs.h>

typedef struct {
	uint32 nfiles;
} initrd_header_t;

extern fs_node_t *initrd_root;

/* Also defined in create_initrd.c */
typedef struct {
	uint8 magic;
	char name[64];
	uint32 offset; /* how far into the initrd image this file is located */
	uint32 length; /* size of the file, in bytes */
} initrd_file_header_t;

/* Creates the initrd; the argument is the memory location of the multiboot module; the return value is the VFS structure */
fs_node_t *init_initrd(uint32 addr);

#endif
