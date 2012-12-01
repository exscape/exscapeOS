#include <kernel/vfs.h>
#include <types.h>
#include <kernel/kernutil.h>
#include <kernel/console.h>
#include <kernel/list.h>

/* The root of the filesystem hierarchy */
fs_node_t *fs_root = NULL;

list_t *mountpoints = NULL;

mountpoint_t *find_mountpoint_for_path(const char *path) {
	if (mountpoints == NULL || mountpoints->count == 0 || path == NULL)
		return NULL;

	/* TODO: We only support the root mountpoint for now... */
	return (mountpoint_t *)mountpoints->head->data;
}

uint32 fsize(fs_node_t *node) {
	assert(node->fsize != NULL);
	return node->fsize(node);
}

/* Calls the correct read function */
uint32 read_fs(fs_node_t *node, uint32 offset, uint32 size, uint8 *buffer) {
	assert(node->read != NULL);
	return node->read(node, offset, size, buffer);
}

/* Calls the correct write function */
uint32 write_fs(fs_node_t *node, uint32 offset, uint32 size, uint8 *buffer) {
	assert(node->write != NULL);
	return node->write(node, offset, size, buffer);
}

/* Calls the correct open function */
void open_fs(fs_node_t *node, uint8 read __attribute__((unused)), uint8 write __attribute__((unused))) {
	assert(node->open != NULL);
	return node->open(node);
}

/* Calls the correct close function */
void close_fs(fs_node_t *node) {
	assert(node->close != NULL);
	return node->close(node);
}

/* Calls the correct readdir function */
struct dirent *readdir_fs(fs_node_t *node, uint32 index) {
	assert((node->flags & 0x7) == FS_DIRECTORY);
	assert(node->readdir != NULL);
	return node->readdir(node, index);
}

/* Calls the correct finddir function */
fs_node_t *finddir_fs(fs_node_t *node, const char *name) {
	assert((node->flags & 0x7) == FS_DIRECTORY);
	assert(node->finddir != NULL);
	return node->finddir(node, name);
}
