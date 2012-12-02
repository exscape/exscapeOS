#include <kernel/initrd.h>
#include <kernel/vfs.h>
#include <types.h>
#include <kernel/kernutil.h>
#include <string.h>
#include <kernel/heap.h>

initrd_header_t *initrd_header;     /* the initrd image header (number of files in the image) */
initrd_file_header_t *file_headers; /* array of headers, one for each file in the initrd */
fs_node_t *initrd_root;             /* the root (/) node */
fs_node_t *root_nodes;              /* array of the file nodes in the root directory */
sint32 nroot_nodes;                 /* number of entries in the above array */

/* TODO: is this really a good idea? */
struct dirent dirent;

// Returns the file size
static uint32 initrd_fsize(fs_node_t *node) {
	initrd_file_header_t header = file_headers[node->inode];
	return header.length;
}

/* The read function used by our initrd filesystem */
static uint32 initrd_read(fs_node_t *node, uint32 offset, uint32 size, uint8 *buffer) {
	initrd_file_header_t header = file_headers[node->inode];

	/* We can't read outside the file! */
	if (offset > header.length)
		return 0;

	/* Adjust the size down if needed */
	if (offset + size > header.length)
		size = header.length - offset;

	if (size == 0)
		return 0;

	memcpy(buffer, (uint8 *)(header.offset + offset), size);

	return size;
}

/* The readdir function for the initrd filesystem */
static struct dirent *initrd_readdir(fs_node_t *node, uint32 index) {
	if (nroot_nodes == 0)
		return 0;

	/* Return 0 if the caller tries to read past the last node */
	if (index > 0 && index >= (uint32)nroot_nodes)
		return 0;

	strlcpy(dirent.d_name, root_nodes[index].name, sizeof(dirent.d_name));
	dirent.d_ino = root_nodes[index].inode;
	return &dirent;
}

/* Locates a directory entry (aka file, since the FS supports nothing else) by name */
static fs_node_t *initrd_finddir(fs_node_t *node, const char *name) {
	for (uint32 i = 0; i < (uint32)nroot_nodes; i++) {
		if (!strcmp(name, root_nodes[i].name))
			return &root_nodes[i];
	}

	return 0;
}

/* Fetches the initrd from the location specified (provided by GRUB),
 * and sets up the necessary structures. */
fs_node_t *init_initrd(uint32 location) {
	initrd_header = (initrd_header_t *)location;
	file_headers = (initrd_file_header_t *)(location + sizeof(initrd_header_t));

	/* Set up the root directory */
	initrd_root = (fs_node_t *)kmalloc(sizeof(fs_node_t));
	memset(initrd_root, 0, sizeof(fs_node_t));
	strlcpy(initrd_root->name, "initrd", sizeof(initrd_root->name));

	initrd_root->flags = FS_DIRECTORY;
	initrd_root->readdir = &initrd_readdir;
	initrd_root->finddir = &initrd_finddir;
	/* the rest of the struct is 0, including the function pointers for read(), write() etc. */

	/* set up the files that reside in the initrd filesystem */
	root_nodes = (fs_node_t *)kmalloc(sizeof(fs_node_t) * initrd_header->nfiles);
	memset(root_nodes, 0, sizeof(fs_node_t) * initrd_header->nfiles);
	nroot_nodes = initrd_header->nfiles;

	/* Set up each individual file */
	for (uint32 i = 0; i < initrd_header->nfiles; i++) {
		/* Change the offset value to be relative to the start of memory, 
		 * rather than the start of the ramdisk/initrd image */
		file_headers[i].offset += location;

		/* create a vfs node for this file */
		strlcpy(root_nodes[i].name, file_headers[i].name, sizeof(root_nodes[i].name));
		root_nodes[i].length = file_headers[i].length;
		root_nodes[i].inode = i;
		root_nodes[i].flags = FS_FILE;
		root_nodes[i].read = &initrd_read;
		root_nodes[i].fsize = &initrd_fsize;
		/* the rest of the function pointers, including write/open/close are left as NULL */
	}

	return initrd_root;
}
