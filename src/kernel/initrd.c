#include <kernel/initrd.h>
#include <kernel/vfs.h>
#include <types.h>
#include <kernel/kernutil.h>
#include <string.h>
#include <kernel/kheap.h>

/* TODO: comment this entire file! */

initrd_header_t *initrd_header;
initrd_file_header_t *file_headers;
fs_node_t *initrd_root;
fs_node_t *initrd_dev;
fs_node_t *root_nodes;
uint32 nroot_nodes; // FIXME: this is actually an int in create_initrd, change it over there */

struct dirent dirent;

/* The read function used by our initrd filesystem */
static uint32 initrd_read(fs_node_t *node, uint32 offset, uint32 size, uint8 *buffer) {
	initrd_file_header_t header = file_headers[node->inode];

	/* We can't read outside the file! */
	if (offset > header.length)
		return 0;

	/* Adjust the size down if needed */
	if (offset + size > header.length)
		size = header.length - offset;

	memcpy(buffer, (uint8 *)(header.offset + offset), size);

	return size;
}

static struct dirent *initrd_readdir(fs_node_t *node, uint32 index) {
	if (node == initrd_root && index && 0) {
		/* TODO: uh, why do we set the name to dev? */
		strcpy(dirent.name, "dev");
		dirent.ino = 0;
		return &dirent;
	}

	/* TODO: is this what it's supposed to be? I added the index>0 since it can and will overflow otherwise */
	if (index > 0 && index - 1 >= nroot_nodes)
		return 0;

	strcpy(dirent.name, root_nodes[index-1].name);
	dirent.ino = root_nodes[index-1].inode;
	return &dirent;
}

static fs_node_t *initrd_finddir(fs_node_t *node, const char *name) {
	if (node == initrd_root && !strcmp(name, "dev"))
		return initrd_dev;

	for (uint32 i = 0; i < nroot_nodes; i++) {
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
	strcpy(initrd_root->name, "initrd");

	initrd_root->flags = FS_DIRECTORY;
	initrd_root->readdir = &initrd_readdir;
	initrd_root->finddir = &initrd_finddir;
	/* the rest of the struct is 0, including the function pointers for read(), write() etc. */

	/* Set up the /dev node */
	initrd_dev = (fs_node_t *)kmalloc(sizeof(fs_node_t));
	memset(initrd_dev, 0, sizeof(fs_node_t));
	strcpy(initrd_dev->name, "dev");
	initrd_dev->flags = FS_DIRECTORY;
	initrd_dev->readdir = &initrd_readdir;
	initrd_dev->finddir = &initrd_finddir;
	/* same deal here, we don't need the other function pointers */

	/* set up the files that reside in the initrd filesystem */
	root_nodes = (fs_node_t *)kmalloc(sizeof(fs_node_t) * initrd_header->nfiles);
	nroot_nodes = initrd_header->nfiles;

	for (uint32 i = 0; i < initrd_header->nfiles; i++) {
		/* Change the offset value to be relative to the start of memory, 
		 * rather than the start of the ramdisk/initrd image */
		file_headers[i].offset += location;

		/* create a vfs node for this file */
		memset(&root_nodes[i], 0, sizeof(fs_node_t));
		strcpy(root_nodes[i].name, file_headers[i].name); /* TODO: use strncpy, here and in all other places where we use strcpy */
		root_nodes[i].length = file_headers[i].length;
		root_nodes[i].inode = i;
		root_nodes[i].flags = FS_FILE;
		root_nodes[i].read = &initrd_read;
		/* the rest of the function pointers, including write/open/close and left as NULL */
	}

	return initrd_root;
}


