#include <kernel/vfs.h>
#include <types.h>
#include <kernel/kernutil.h>
#include <kernel/console.h>
#include <kernel/list.h>
#include <string.h>

/* The root of the filesystem hierarchy */
fs_node_t *fs_root = NULL;

list_t *mountpoints = NULL;

mountpoint_t *find_mountpoint_for_path(const char *path) {
	if (mountpoints == NULL || mountpoints->count == 0 || path == NULL)
		return NULL;

	/*
	 * Simple but ugly: find the mountpoint which matches as much as possible
	 * of the path. For example, the mountpoint "/data/x/y" matches more than
	 * just "/data" of the path "/data/x/y/file.txt", so the longer one is used.
	 */

	mountpoint_t *best_match = NULL;
	size_t best_match_len = 0;

	for (node_t *it = mountpoints->head; it != NULL; it = it->next) {
		mountpoint_t *mp = (mountpoint_t *)it->data;
		size_t len = strlen(mp->path);
		if (strlen(path) < len)
			continue;

		if (strnicmp(mp->path, path, len) == 0) {
			// First part matches!
			if (len > best_match_len) {
				best_match = mp;
				best_match_len = len;
			}
		}
	}

	return best_match;
}

#if 0

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
void open_fs(fs_node_t *node, uint8 _read __attribute__((unused)), uint8 _write __attribute__((unused))) {
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
#endif
