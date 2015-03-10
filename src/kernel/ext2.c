#include <sys/types.h>
#include <kernel/ext2.h>
#include <kernel/kernutil.h>
#include <kernel/console.h>
#include <kernel/serial.h>
#include <kernel/heap.h>
#include <kernel/list.h>
#include <string.h>
#include <kernel/partition.h>
#include <kernel/vfs.h>
#include <path.h>
#include <sys/errno.h>
#include <kernel/pmm.h>
#include <kernel/time.h>
#include <kernel/backtrace.h>

list_t *ext2_partitions = NULL;

#define min(a,b) ( (a < b ? a : b) )

// Used to pass extra arguments in inode_for_path (of varying types, cast as uint32)
struct param {
	int operation;
	uint32 arg1;
	uint32 arg2;
	uint32 arg3;
};

static int ext2_open(uint32 dev, const char *path, int mode);
static int ext2_read(int fd, void *buf, size_t length);
static int ext2_close(int fd, struct open_file *file);
static int ext2_fstat(int fd, struct stat *buf);
static int ext2_getdents(int fd, void *dp, int count);
static int ext2_stat(mountpoint_t *mp, const char *path, struct stat *st);
static int ext2_lstat(mountpoint_t *mp, const char *path, struct stat *st);
static DIR *ext2_opendir(mountpoint_t *mp, const char *path);
ssize_t ext2_readlink(mountpoint_t *mp, const char *pathname, char *buf, size_t bufsiz);

static struct inode_ret inode_for_path(ext2_partition_t *part, const char *path, struct param *op_param);

// Used by ext2_getdents to store state, as we only have a single void* in struct open_file to store state in
struct ext2_getdents_info {
	char *dir_buf;
	uint32 len;
	uint32 pos;
};

// Used by inode_for_path and functions that use it (e.g.  ext2_open and ext2_stat).
// If inode_for_path encounters a symlink, it needs to re-call open() or stat(), VFS-wide
// (since the symlink may be to a different and even non-ext2 partition).
// It does this by setting setting "value" to the result of open() or stat(), and
// setting type to indicate this.
//
// In cases without symlinks, "value" is simply the inode number.
#define TYPE_INODE  1
#define TYPE_RETVAL 2
struct inode_ret {
	uint32 value; // only valid if type == TYPE_INODE
	int type;
};
#define OPERATION_OPEN 1
#define OPERATION_STAT 2
#define OPERATION_LSTAT 3
#define OPERATION_READLINK 4

static int ext2_stat_inode(ext2_partition_t *part, mountpoint_t *mp, struct stat *st, uint32 inode_num);

char *ext2_read_file(ext2_partition_t *part, uint32 inode_num, uint32 *size); // read an ENTIRE FILE and return a malloc'ed buffer with it

static uint32 block_to_abs_lba(ext2_partition_t *part, uint32 block) {
	assert(part != NULL);
	uint32 sectors_per_block = (1024 << part->super.s_log_block_size) / 512;
	return part->part->start_lba + block * sectors_per_block;
}

static uint32 bgrp_for_inode(ext2_partition_t *part, uint32 inode) {
	assert(part != NULL);
	return (inode - 1) / part->super.s_inodes_per_group;
}

static uint32 local_index_for_inode(ext2_partition_t *part, uint32 inode) {
	assert(part != NULL);
	return (inode - 1) % part->super.s_inodes_per_group;
}

static void ext2_read_inode(ext2_partition_t *part, uint32 inode, void *buf) {
	assert(part != NULL);
	assert(inode >= EXT2_ROOT_INO);
	assert(buf != NULL);

	uint32 bgrp = bgrp_for_inode(part, inode);
	uint32 index = local_index_for_inode(part, inode);

	ext2_bgd_t *bgd = part->bgdt + bgrp;
	uint32 inode_table_block = bgd->bg_inode_table;

	// byte offset into the inode table
	uint32 offset = index * sizeof(ext2_inode_t);
	uint32 block_offset = offset / part->blocksize;
	offset %= part->blocksize;

	assert(offset % sizeof(ext2_inode_t) == 0);

	char *inode_buf = kmalloc(512);
	int ret = ata_read(part->dev, block_to_abs_lba(part, inode_table_block + block_offset) + offset/512, inode_buf, 1);
	assert(ret != 0);
	memcpy(buf, inode_buf + (offset % 512), sizeof(ext2_inode_t));
	kfree(inode_buf);
}

static uint32 read_direct_blocks(ext2_partition_t *part, uint32 *blocklist, uint32 num, void *buf) {
	assert(part != NULL);
	assert(blocklist != NULL);
	assert(*blocklist != 0);
	assert(num > 0);
	assert(buf != NULL);

	// For each block number in the array, read the data in that block into the buffer,
	// assuming we haven't reached the end yet.
	for (uint32 i = 0; i < num; i++) {
		if (blocklist[i] == 0) {
			continue;
		}
		int ret = ata_read(part->dev, block_to_abs_lba(part, blocklist[i]), (char *)buf + i * part->blocksize, part->blocksize / 512);
		assert(ret != 0);
	}

	return num;
}

static uint32 read_singly_indirect_blocks(ext2_partition_t *part, uint32 singly_block, uint32 max_num, void *buf) {
	assert(part != NULL);
	assert(singly_block > EXT2_ROOT_INO);
	assert(max_num <= part->blocksize / 4); // This may be relaxed later, in case blocklists are consecutive on disk
	assert(buf != NULL);
	
	// To begin with, we read the contents of the indirect block into a buffer;
	// this is really just an array of uint32s.
	uint32 *blocklist = kmalloc(part->blocksize);
	int ret =ata_read(part->dev, block_to_abs_lba(part, singly_block), blocklist, part->blocksize / 512);
	assert(ret != 0);

	// Next, read the blocks.
	read_direct_blocks(part, blocklist, max_num, buf);

	kfree(blocklist);

	return max_num;
}

static uint32 read_doubly_indirect_blocks(ext2_partition_t *part, uint32 doubly_block, uint32 num, void *buf) {
	assert(part != NULL);
	assert(doubly_block > EXT2_ROOT_INO);
	assert(num <= (part->blocksize/4) * (part->blocksize/4));
	assert(buf != NULL);

	uint32 *singly_blocks = kmalloc(part->blocksize);
	int ret = ata_read(part->dev, block_to_abs_lba(part, doubly_block), singly_blocks, part->blocksize / 512);
	assert(ret != 0);

	// Next, read through as many of these singly indirect blocks as required.
	uint32 read_data_blocks = 0;
	for (uint32 i = 0; num > read_data_blocks && i < part->blocksize/4; i++) {
		uint32 singly = singly_blocks[i];
		if (singly == 0)
			continue;
		uint32 data_blocks_to_read = min(num - read_data_blocks, part->blocksize/4);
		read_data_blocks += read_singly_indirect_blocks(part, singly, data_blocks_to_read, (char *)buf + read_data_blocks * part->blocksize);
	}
	kfree(singly_blocks);

	return read_data_blocks;
}

static uint32 read_triply_indirect_blocks(ext2_partition_t *part, uint32 triply_block, uint32 num, void *buf) {
	assert(part != NULL);
	assert(triply_block > EXT2_ROOT_INO);
	assert(buf != NULL);

	uint32 *doubly_blocks = kmalloc(part->blocksize);
	int ret = ata_read(part->dev, block_to_abs_lba(part, triply_block), doubly_blocks, part->blocksize / 512);
	assert(ret != 0);

	// Next, read through as many of these doubly indirect blocks as required.
	uint32 read_data_blocks = 0;
	for (uint32 i = 0; num > read_data_blocks && i < part->blocksize/4; i++) {
		uint32 doubly = doubly_blocks[i];
		if (doubly == 0)
			continue;
		uint32 data_blocks_to_read = min(num - read_data_blocks, part->blocksize/4 * part->blocksize/4); // how many data blocks to read from THIS doubly indir block
		read_data_blocks += read_doubly_indirect_blocks(part, doubly, data_blocks_to_read, (char *)buf + read_data_blocks * part->blocksize);
	}
	kfree(doubly_blocks);

	return read_data_blocks;
}

char *ext2_read_file(ext2_partition_t *part, uint32 inode_num, uint32 *size /* out */) {
	assert(part != NULL);
	assert(inode_num >= EXT2_ROOT_INO);

	ext2_inode_t *inode = kmalloc(sizeof(ext2_inode_t));
	ext2_read_inode(part, inode_num, inode);

	uint32 num_blocks = inode->i_blocks/(2 << part->super.s_log_block_size);
	assert(num_blocks > 0);

	if (size)
		*size = inode->i_size;

	char *file_data = kmalloc(num_blocks * part->blocksize);
	memset(file_data, 0, num_blocks * part->blocksize);

	uint32 read_blocks = 0;

	// Begin by reading the 12 direct blocks (or fewer, if the file is small).
	read_direct_blocks(part, &inode->i_direct[0], min(12, num_blocks), file_data);
	read_blocks += min(12, num_blocks);

	if (num_blocks > read_blocks) {
		// The 12 direct blocks weren't enough, so we'll have to use singly indirect ones.
		uint32 blocks_to_read = min(num_blocks - read_blocks, part->blocksize/4);
		read_singly_indirect_blocks(part, inode->i_singly, blocks_to_read, file_data + read_blocks * part->blocksize);
		read_blocks += blocks_to_read;
	}

	if (num_blocks > read_blocks) {
		// The 12 direct + (blocksize/4) singly indirect ones weren't enough, either!
		uint32 blocks_to_read = min(num_blocks - read_blocks, part->blocksize/4 * part->blocksize/4);
		read_doubly_indirect_blocks(part, inode->i_doubly, blocks_to_read, file_data + read_blocks * part->blocksize);
		read_blocks += blocks_to_read;
	}

	if (num_blocks > read_blocks) {
		// Triply indirect blocks are required to read this file.
		uint32 blocks_to_read = num_blocks - read_blocks;
		read_triply_indirect_blocks(part, inode->i_triply, blocks_to_read, file_data + read_blocks * part->blocksize);
		read_blocks += blocks_to_read;
	}

	kfree(inode);

	return file_data;
}

bool ext2_detect(ata_device_t *dev, uint8 part) {
	/* Quite a few sanity checks */
	assert(dev != NULL);
	assert(dev->exists);
	assert(!dev->is_atapi);
	assert(part <= 3);
	assert(dev->partition[part].exists);
	assert(dev->partition[part].type == PART_LINUX);

	uint8 buf[1024] = {0};
	/* Read the superblock, 1024 bytes (2 sectors) in, 1024 bytes (2 sectors) long */
	int ret = ata_read(dev, dev->partition[part].start_lba + 2, buf, 2);
	assert(ret != 0);

	/* Create the list of partitions if it doesn't already exist) */
	if (ext2_partitions == NULL)
		ext2_partitions = list_create();
	/* Same for the mountpoints list */
	if (mountpoints == NULL)
		mountpoints = list_create();

	/* Set up an entry */
	ext2_partition_t *part_info = kmalloc(sizeof(ext2_partition_t));
	memset(part_info, 0, sizeof(ext2_partition_t));
	part_info->part = &dev->partition[part];
	part_info->dev = dev;

	memcpy(&part_info->super, buf, 1024);
	part_info->blocksize = 1024 << part_info->super.s_log_block_size;

	uint32 bgdt_size = (part_info->super.s_blocks_count / part_info->super.s_blocks_per_group + 1) * sizeof(ext2_bgd_t);
	uint32 num_bgdt_sectors = (bgdt_size % 512 == 0) ? bgdt_size/512 : bgdt_size/512 + 1;

	ext2_bgd_t *bgd = kmalloc(num_bgdt_sectors * 512);
	ret = ata_read(dev, block_to_abs_lba(part_info, part_info->super.s_log_block_size == 0 ? 2 : 1), bgd, num_bgdt_sectors);
	assert(ret != 0);
	part_info->bgdt = bgd; // Array of block group descriptors

	// Set up the mountpoint
	mountpoint_t *mp = kmalloc(sizeof(mountpoint_t));
	memset(mp, 0, sizeof(mountpoint_t));
	mp->path[0] = 0; // not set up here
	mp->mpops.open     = ext2_open;
	mp->mpops.opendir  = ext2_opendir;
	mp->mpops.stat     = ext2_stat;
	mp->mpops.lstat     = ext2_lstat;
	mp->mpops.readlink     = ext2_readlink;
	mp->dev = next_dev; // increased below
	list_append(mountpoints, mp);

	// Store this in the device table (used for dev ID number -> partition mappings)
	devtable[next_dev++] = (void *)part_info;

	/* Add the new partition entry to the list */
	list_append(ext2_partitions, part_info);

	return true;
}

static ssize_t ext2_readlink_inode(ext2_partition_t *part, uint32 inode_num, ext2_inode_t *inode, char *link_path, uint32 bufsize) {
	// Given a partition and an inode struct, figures out the link path and writes it to the link_path buffer.
	assert(part != NULL);
	assert(inode_num >= EXT2_ROOT_INO);
	assert(inode != NULL);
	assert(link_path != NULL);
	assert(bufsize != 0);

	if (!EXT2_ISLNK(inode->i_mode))
		return -EINVAL;

	if (inode->i_size + 1 > bufsize) {
		return -ENAMETOOLONG;
	}

	if (inode->i_size <= 60 && inode->i_blocks == 0) {
		// This is a "fast symbolic link", which is stored in the inode itself,
		// where the block pointers would otherwise be. Awesome, because that
		// makes our job extremely simple.
		memcpy(link_path, (char *)&inode->i_direct[0], inode->i_size);
		link_path[inode->i_size] = 0;
		return (ssize_t)inode->i_size;
	}

	assert(inode->i_blocks > 0);
	assert(inode->i_direct[0] > EXT2_ROOT_INO);

	// No such luck... but we still have a relatively easy job, thanks to helper functions.
	uint32 size = 0;
	char *buf = ext2_read_file(part, inode_num, &size);
	assert(inode->i_size == size);

	memcpy(link_path, buf, inode->i_size);
	link_path[inode->i_size] = 0;
	kfree(buf);

	return (ssize_t)size;
}

ssize_t ext2_readlink(struct mountpoint *mp, const char *pathname, char *buf, size_t bufsiz) {
	assert(mp != NULL);
	assert(pathname != NULL);
	assert(buf != NULL);
	assert(bufsiz > 0);

	// Note: This function must NOT null terminate, as per the spec.
	// ext2_readlink_inode does.

	ext2_partition_t *part = (ext2_partition_t *)devtable[mp->dev];
	assert(part != NULL);

	struct param op_param;
	op_param.operation = OPERATION_READLINK;
	op_param.arg1 = (uint32)buf;
	op_param.arg2 = (uint32)bufsiz;

	struct inode_ret _ino = inode_for_path(part, pathname, &op_param);

	if (_ino.type == TYPE_RETVAL)
		return _ino.value;

	assert(_ino.type == TYPE_INODE);

	if (_ino.value < EXT2_ROOT_INO)
		return -ENOENT;

	// OK, we have _ino.value; use it to read the inode data for this inode number
	ext2_inode_t *inode = kmalloc(sizeof(ext2_inode_t));
	ext2_read_inode(part, _ino.value, inode);

	char *link = kmalloc(PATH_MAX+1);
	memset(link, 0, PATH_MAX+1);
	ssize_t size = ext2_readlink_inode(part, _ino.value, inode, link, PATH_MAX);
	kfree(inode);

	if (size < 0) {
		kfree(link);
		return size;
	}

	// There! We can now finally copy this back to userspace.
	// We need to make sure to NOT null terminate here.
	memcpy(buf, link, min(size, (ssize_t)bufsiz));
	kfree(link);

	return min(size, (ssize_t)bufsiz);
}

struct parent_info {
	char *full_path;
	int depth;
};

static char *calculate_parent(ext2_partition_t *part, const struct parent_info *parent_info) {
	assert(part != NULL);
	assert(parent_info->full_path != NULL);

	char *parent_dir = kmalloc(PATH_MAX+1);
	memset(parent_dir, 0, PATH_MAX+1);

	// Calculate the parent directory from the full path, and the recursion depth in _inode_for_path (0 for the first call)
	// For example: With a FS mounted on /mnt/ext2, mp->depth = 2 (/ would be 0, /mnt would be 1).
	// If full_path is /mnt/ext2/dir1/dir2/file, this would return:
	// /mnt/ext2 if depth = 0,
	// /mnt/ext2/dir1 if depth = 1, and
	// /mnt/ext2/dir1/dir2 if depth = 2.
	// depth > 2 would be invalid in this case.
	strlcpy(parent_dir, parent_info->full_path, PATH_MAX+1);
	char *p = parent_dir;
	for (int i = 0; i < parent_info->depth + part->mp->depth; i++) {
		assert(p != NULL);
		p++;
		p = strchr(p, '/');
	}
	assert(p != NULL);
	*p = 0;

	return parent_dir;
}

static struct inode_ret ext2_handle_operation(ext2_partition_t *part, const char *full_path, uint32 inode_num, struct param *op_param) {
	assert(part != NULL);
	assert(full_path != NULL);
	assert(inode_num >= EXT2_ROOT_INO);
	assert(op_param != NULL);

	struct inode_ret ret = {0};

	if (current_task->link_count >= 6) {
		// Too many nested links; time to get out, before
		// we get a double fault (due to a kernel stack overflow)
		ret.value = -ELOOP;
		ret.type = TYPE_RETVAL;
		return ret;
	}

	if (op_param->operation == OPERATION_OPEN) {
		current_task->link_count++;
		ret.value = open(full_path, (int)op_param->arg1);
		current_task->link_count--;
		ret.type = TYPE_RETVAL;
	}
	else if (op_param->operation == OPERATION_STAT) {
		current_task->link_count++;
		ret.value = stat(full_path, (struct stat *)op_param->arg1);
		current_task->link_count--;
		ret.type = TYPE_RETVAL;
	}
	else if (op_param->operation == OPERATION_LSTAT) {
		// Stat *this* inode (or we might follow a symlink when we shouldn't)
		// and return the value
		ret.value = ext2_stat_inode(part, part->mp, (struct stat *)op_param->arg1, inode_num);
		ret.type = TYPE_RETVAL;
	}
	else if (op_param->operation == OPERATION_READLINK) {
		// Simply return the value of the inode *to this link*; ext2_readlink_inode does the rest
		ret.type = TYPE_INODE;
		ret.value = inode_num;
	}
	else
		panic("Unsupported operation in ext2_handle_operation");

	return ret;
}

static struct inode_ret ext2_handle_symlink_in_path(ext2_partition_t *part, const char *path, const char *link_path, struct parent_info *parent_info, uint32 parent_inode_num, struct param *op_param) {
	assert(part != NULL);
	assert(link_path != NULL);
	assert(*link_path != 0);
	assert(parent_info != NULL);
	assert(parent_info->full_path != NULL);
	assert(*parent_info->full_path != 0);

	// For relative symbolic links, we need to know the full path to the parent directory
	// of the link, so that we can apply relative paths relative to that.
	char *parent_dir = calculate_parent(part, parent_info);
	assert(*parent_dir == '/');

	char *full_path = kmalloc(PATH_MAX+1);
	memset(full_path, 0, PATH_MAX+1);

	if (*link_path == '/') {
		// Absolute path
		strcpy(full_path, link_path);
	}
	else
		strcpy(full_path, parent_dir);

	// Resolve *all* possible symlinks in the path and check for symlink loops and
	// overly nested links
	if (resolve_actual_path(full_path, PATH_MAX+1) == -ELOOP) {
		struct inode_ret ret;
		ret.value = -ELOOP;
		ret.type = TYPE_RETVAL;
		kfree(full_path);
		kfree(parent_dir);
		return ret;
	}

	// Join the symlinked path with the rest (e.g. the file name to open, "c" in the example above)
	path_join(full_path, path);

	struct inode_ret ret = ext2_handle_operation(part, full_path, parent_inode_num, op_param);
	kfree(full_path);
	kfree(parent_dir);
	return ret;
}

// Find the inode number for a given path by looking recursively, starting at,
// with help from inode_for_path(), the root directory.
// This should only ever be called by inode_for_path.
static struct inode_ret _inode_for_path(ext2_partition_t *part, const char *path, struct parent_info *parent_info, uint32 parent_inode_num, struct param *op_param) {
	assert(part != NULL);
	assert(path != NULL);
	assert(parent_inode_num >= EXT2_ROOT_INO);
	assert(current_task->link_count >= 0);

	char *cur_entry = kmalloc(PATH_MAX+1);
	memset(cur_entry, 0, PATH_MAX+1);
	strlcpy(cur_entry, path, PATH_MAX+1);

	if (strchr(cur_entry, '/'))
		*strchr(cur_entry, '/') = 0;

	// Time to get to work.
	// First, we read the parent directory's inode.
	// If it's in fact a symlink, try to resolve it.
	// In that case, the helper function will redirect the
	// open, stat etc. call and return a result, rather than an inode.
	// This is because it might point to other filesystems, where
	// an ext2 inode number wouldn't be possible to find.
	// I'm not very happy with this, but until I feel like doing a proper
	// VFS rewrite, it'll have to work. At least it's cleaned up now
	// as opposed to how it was until ~5 Dec 2014.

	ext2_inode_t *parent_inode = kmalloc(sizeof(ext2_inode_t));
	ext2_read_inode(part, parent_inode_num, parent_inode);

	if (EXT2_ISLNK(parent_inode->i_mode)) {
		// This is a symbolic link, used as a directory in a sub-path,
	   //  e.g. /a/b/c where the symlink is a or b, but *not* c.
		char *link_path = kmalloc(PATH_MAX+1);
		ext2_readlink_inode(part, parent_inode_num, parent_inode, link_path, PATH_MAX+1);

		struct inode_ret ret = ext2_handle_symlink_in_path(part, path, link_path, parent_info, parent_inode_num, op_param);
		kfree(parent_inode);
		kfree(link_path);
		kfree(cur_entry);
		return ret;
	}

	// This wasn't a symlink; if it's not a directory, either, then
	// it cannot be used as part of a path.
	if (!EXT2_ISDIR(parent_inode->i_mode)) {
		struct inode_ret ret;
		ret.type = TYPE_RETVAL;
		ret.value = -ENOTDIR;
		kfree(parent_inode);
		kfree(cur_entry);
		return ret;
	}

	kfree(parent_inode); parent_inode = NULL;

	// When we've gotten this far, the parent directory was an actual directory.
	// Time to read it, and attempt to find the next path companent (dir, symlink or file)
	// inside the directory entry.

	uint32 size;
	ext2_direntry_t *dir = (ext2_direntry_t *)ext2_read_file(part, parent_inode_num, &size);
	ext2_direntry_t * const orig_ptr = dir; // required for kfree, as we modify dir() below, and thus can't pass it to kfree

	uint32 i = 0;
	do {
		if (i < size && dir->inode == 0) {
			// This directory entry is empty, but we haven't read
			// all of the directory's contents. Skip to the next one.
			assert(i % part->blocksize == 0);
			dir = (ext2_direntry_t *)( (char *)dir + part->blocksize);
			i += part->blocksize;
			continue;
		}
		else if (i >= size)
			panic("ext2: read past end of directory!");

		if (strlen(cur_entry) == dir->name_len && memcmp(cur_entry, dir->name, dir->name_len) == 0) {
			// Found it -- either this is the final part of the path (handled just below),
			// or it's a path component (handled in the else case).

			if (strcmp(path, cur_entry) == 0) {
				// Found the final path component!
				// It could still be a symlink, though.
				if (dir->file_type == EXT2_FT_SYMLINK) {
					// Yup, it is.
					ext2_inode_t *tmp_inode = kmalloc(sizeof(ext2_inode_t));
					ext2_read_inode(part, dir->inode, tmp_inode);

					// OK, so we now have the inode info. Find out where it points.
					char *path_buf = kmalloc(PATH_MAX+1);
					memset(path_buf, 0, PATH_MAX+1);
					ext2_readlink_inode(part, dir->inode, tmp_inode, path_buf, PATH_MAX+1);
					kfree(tmp_inode); tmp_inode = NULL;

					// path_buf is the path we need to open. However, it may be a relative path.
					// If so, we CANNOT simply call open/stat/etc.; the reason is that the path
					// should be relative to *its parent directory*, not relative to $PWD!

					char *full_path = kmalloc(PATH_MAX+1);
					memset(full_path, 0, PATH_MAX+1);

					if (*path_buf == '/') {
						// Absolute path
						strcpy(full_path, path_buf);
					}
					else {
						char *parent_dir = calculate_parent(part, parent_info);
						assert(*parent_dir == '/');

						strcpy(full_path, parent_dir);
						kfree(parent_dir);
						if (resolve_actual_path(full_path, PATH_MAX+1) == -ELOOP) {
							struct inode_ret ret;
							ret.value = -ELOOP;
							ret.type = TYPE_RETVAL;
							kfree(tmp_inode);
							kfree(orig_ptr);
							kfree(path_buf);
							kfree(cur_entry);
							kfree(full_path);
							return ret;
						}
						path_join(full_path, path_buf);
					}
					kfree(path_buf);

					struct inode_ret ret = ext2_handle_operation(part, full_path, dir->inode, op_param);
					kfree(full_path);
					kfree(tmp_inode);
					kfree(orig_ptr);
					kfree(cur_entry);
					return ret;
				}
				else {
					// Not a symlink; return the inode number
					struct inode_ret ret = {0};
					ret.value = dir->inode;
					ret.type = TYPE_INODE;
					kfree(cur_entry);
					kfree(orig_ptr);
					return ret;
				}
			}
			else if (strchr(path, '/')) {
				// We didn't find the file itself, but a path component.
				// Recurse and keep going deeper into the directory structure.
				kfree(orig_ptr);
				kfree(cur_entry);
				parent_info->depth++;
				return _inode_for_path(part, strchr(path, '/') + 1, parent_info, dir->inode, op_param);
			}
			else
				panic("end of path, but file not found");
		}

		i += dir->rec_len;
		dir = (ext2_direntry_t *)((char *)dir + dir->rec_len);
	} while(i < size);

	kfree(orig_ptr);
	kfree(cur_entry);

	struct inode_ret ret;
	ret.value = 0; // not found
	ret.type = TYPE_INODE;
	return ret;
}

static struct inode_ret inode_for_path(ext2_partition_t *part, const char *path, struct param *op_param) {
	assert(part != NULL);
	assert(path != NULL);

	if (strcmp(path, "/") == 0) {
		struct inode_ret ret = {0};
		ret.value = EXT2_ROOT_INO;
		ret.type = TYPE_INODE;
		return ret;
	}
	else if (*path == '/') {
		// Figure out the absolute path (where / is the true root, i.e. not relative to the mountpoint)
		// and send that info along to _inode_for_path. The "path" argument itself *is* relative
		// to the mountpoint, however; only parent_info is not.
		struct parent_info parent_info;
		parent_info.full_path = kmalloc(PATH_MAX+1);
		memset(parent_info.full_path, 0, PATH_MAX+1);
		strcpy(parent_info.full_path, part->mp->path);
		path_join(parent_info.full_path, path);
		parent_info.depth = 0;

		struct inode_ret ret = _inode_for_path(part, path+1, &parent_info, EXT2_ROOT_INO, op_param);
		kfree(parent_info.full_path);
		return ret;
	}
	else
		panic("Invalid path %s given to inode_for_path (paths must be absolute)", path);
}

// Used by ext2_stat and ext2_fstat to avoid code duplication.
// Those two simply find the inode number, and call this.
static int ext2_stat_inode(ext2_partition_t *part, mountpoint_t *mp, struct stat *st, uint32 inode_num) {
	assert(part != NULL);
	assert(mp != NULL);
	assert(st != NULL);
	assert(inode_num >= EXT2_ROOT_INO);

	ext2_inode_t *inode = kmalloc(sizeof(ext2_inode_t));
	ext2_read_inode(part, inode_num, inode);

	memset(st, 0, sizeof(struct stat));

	st->st_dev = (short)0xffff; // invalid ID, correct is set in loop below
	for (int i=0; i < MAX_DEVS; i++) {
		if ((ext2_partition_t *)devtable[i] == part) {
			st->st_dev = i;
			break;
		}
	}
	assert(st->st_dev != (short)0xffff);

	st->st_ino = inode_num;
	st->st_mode = inode->i_mode; // these appear to be "compatible" (i.e. inode flags = stat flags)
	st->st_nlink = inode->i_links_count;
	st->st_size = inode->i_size;
	st->st_mtime = inode->i_mtime;
	st->st_ctime = inode->i_ctime;
	st->st_atime = inode->i_atime;
	st->st_blksize = part->blocksize;
	st->st_blocks = inode->i_blocks;

	kfree(inode);

	return 0;
}

static int ext2_stat(mountpoint_t *mp, const char *path, struct stat *st) {
	assert(mp != NULL);
	assert(path != NULL);
	assert(st != NULL);

	ext2_partition_t *part = (ext2_partition_t *)devtable[mp->dev];
	assert(part != NULL);

	struct param op_param = {0};
	op_param.operation = OPERATION_STAT;
	op_param.arg1 = (uint32)st;

	struct inode_ret _ino = inode_for_path(part, path, &op_param);

	if (_ino.type == TYPE_INODE && _ino.value == 0)
		return -ENOENT;
	else if (_ino.type == TYPE_RETVAL)
		return _ino.value;

	assert(_ino.type == TYPE_INODE);
	return ext2_stat_inode(part, mp, st, _ino.value);
}

static int ext2_lstat(mountpoint_t *mp, const char *path, struct stat *st) {
	assert(mp != NULL);
	assert(path != NULL);
	assert(st != NULL);

	ext2_partition_t *part = (ext2_partition_t *)devtable[mp->dev];
	assert(part != NULL);

	struct param op_param = {0};
	op_param.operation = OPERATION_LSTAT;
	op_param.arg1 = (uint32)st;
	struct inode_ret _ino = inode_for_path(part, path, &op_param);
	if (_ino.type == TYPE_INODE && _ino.value == 0)
		return -ENOENT;
	else if (_ino.type == TYPE_RETVAL)
		return _ino.value;

	assert(_ino.type == TYPE_INODE);
	return ext2_stat_inode(part, mp, st, _ino.value);
}

// Block path example:
// Say we've read far into a file, and (to avoid needing a ton of examples) are
// far into the triply indirect data.
// If so, we might have read the 12 direct blocks, the e.g. 256 (more with larger blocksizes)
// indirect data blocks, and the e.g. 256*256 doubly indirect blocks.
// Moreover, inside the triply block, we've read the first two (0 and 1) doubly blocks and all their contents,
// and we're now inside the one with index 2. That block specifies 256 singly blocks; we're at index 240.
// Finally, inside that singly indirect block is a list of 256 direct blocks, and we're at index 30.
// We could specify this with:

// .redir_level = TRIPLY_INDIRECT;
// .triply_block = 1234;
// .doubly_block = 2345;
// .singly_block = 3456;
// .direct_index = -1; // only used for redir_level == DIRECT
// .singly_index = 30;
// .doubly_index = 240;
// .triply_index = 2; // index into the triply indirect blocklist, stored at block[15]

typedef struct ext2_blockpath {

} ext2_blockpath_t;

static int ext2_read_block(ext2_partition_t *part, uint32 block, char *buffer) {
	// TODO 
	panic("ext2_read_block called");
	return 0;
}

static int ext2_read(int fd, void *buf, size_t length) {
	struct open_file *file = get_filp(fd);
	if (file == NULL)
		return -EBADF;

	struct stat st;
	int status;
	if ((status = ext2_fstat(fd, &st)) != 0) {
		printk("Warning: fstat failed on opened file in ext2_read; return status = %d on fd %d\n", status, fd);
		return 0;
	}

	uint32 bytes_read = 0; // this call to read() only

	if (S_ISDIR(st.st_mode))
		return -EISDIR;
	if (st.st_size == 0)
		return 0;
	if (file->offset >= file->size)
		goto out;

//	if (file->cur_block == (uint32)NULL) {
		// This is the first read; set up the block path
		// This is used to figure out where to read next.
		// For example, the file's offset may be past all the direct and indirect blocks,
		// and somewhere inside the doubly indirect blocks. We need to know exactly where,
		// since it would be far too slow to re-calculate that on every call to read().
		// TODO: design this
//	}

	ext2_partition_t *part = devtable[file->dev];
	assert(part != NULL);

	const uint32 max_blocks = 32; // How many blocks to read at once (to keep the buffer size down)

	uint8 *block_buf = kmalloc(part->blocksize * max_blocks);

	assert(file->offset >= 0);
//	uint32 local_offset = (uint32)file->offset % part->blocksize;

	// TODO: REMOVE THIS; this is just to shut up a warning temporarily, until this function is implemented
	if (file->offset == 1234)
		goto done;

//	uint32 continuous_blocks = 1;

	/*
	do {
		if (length > part->blocksize) {
			// The request is for more than one block, so at least two need to be
			// read from disk; if they are continuous on disk, we can read them faster
			// by coalescing them into a single disk request.
			uint32 next = 0, cur = file->cur_block;
			while ((next = ext2_next_block(part, cur)) == cur + 1 && \
					continuous_blocks * part->blocksize < length && \
					continuous_blocks < max_blocks)
			{
				cur = next;
				continuous_blocks++;
			}
		}

		uint32 nbytes_read_from_disk = continuous_blocks * part->blocksize;

		int ret = disk_read(part->dev, ext2_lba_from_block(part, file->cur_block), nbytes_read_from_disk, block_buf);
		assert(ret != 0);
		file->cur_block += continuous_blocks - 1; // the last one is taken care of later in all cases

		// We need to stop if either the file size is up, or if the user didn't want more bytes.
		uint32 bytes_copied = min(min(file->size - file->offset, length), nbytes_read_from_disk);

		if (bytes_copied >= nbytes_read_from_disk - local_offset) {
			// We'd read outside the buffer we've read from disk! Limit this read size.
			bytes_copied = nbytes_read_from_disk - local_offset;
		}

		// Copy the data to the buffer
		memcpy((void *)( (uint8 *)buf + bytes_read), block_buf + local_offset, bytes_copied);

		bytes_read += bytes_copied;
		file->offset += bytes_copied;
		local_offset += bytes_copied;

		assert(file->offset <= file->size);

		assert(length >= bytes_copied);
		length -= bytes_copied;

		if (local_offset >= part->blocksize) {
			ino_t next = ext2_next_block(part, file->cur_block);
			if (file->cur_block >= 0x0ffffff8 || file->cur_block < 2) {
				// End of block chain / no data
				assert(file->offset == file->size);
				goto done;
			}
			else
				file->cur_block = next;
		}
		while (local_offset >= part->blocksize) {
			local_offset -= part->blocksize;
		}

		if (length == 0 || file->offset >= file->size)
			break;

	} while (length > 0);
	*/

done:
	kfree(block_buf);
out:
	return bytes_read;
}

static int ext2_fstat(int fd, struct stat *buf) {
	assert(fd >= 0);
	assert(buf != NULL);

    struct open_file *file = get_filp(fd);
	return ext2_stat_inode(devtable[file->dev], file->mp, buf, file->ino);
}

static int ext2_open(uint32 dev, const char *path, int mode) {
	assert(dev <= MAX_DEVS - 1);
	assert(devtable[dev] != NULL);
	assert(path != NULL);

	int fd;
	struct open_file *file = new_filp(&fd);
	if (!file || fd < 0)
		return -EMFILE;

	ext2_partition_t *part = (ext2_partition_t *)devtable[dev];
	assert(part != NULL);

	struct param op_param = {0};
	op_param.operation = OPERATION_OPEN;
	op_param.arg1 = (uint32)mode;

	uint32 inode_num = 0;
	struct inode_ret _ino = inode_for_path(part, path, &op_param);
	if (_ino.type == TYPE_RETVAL) {
		// This instance of ext2_open was pointed towards a symbolic link.
		// _inode_for_path above called open on the actual file,
		// so we can destroy this filp and use that one.
		// TODO: this might break in that it may now appears as if we've opened the file
		// directly, rather than via the symlink.
		destroy_filp(fd);
		return _ino.value;
	}
	else {
		assert(_ino.type == TYPE_INODE);
		inode_num = _ino.value;
	}

	if (inode_num < EXT2_ROOT_INO) {
		destroy_filp(fd);
		return -ENOENT;
	}

	ext2_inode_t *inode = kmalloc(sizeof(ext2_inode_t));
	ext2_read_inode(part, inode_num, inode);

	if (inode_num >= EXT2_ROOT_INO) {
		if ((mode & O_DIRECTORY) && !EXT2_ISDIR(inode->i_mode)) {
			// Caller wanted to open a directory, but this is something else.
			destroy_filp(fd);
			kfree(inode);
			return -ENOTDIR;
		}

		file->dev = dev;
		file->ino = inode_num;
		file->cur_block = (uint32)NULL; // pointer to the current block path
		file->offset = 0;
		file->size = inode->i_size;
		file->mp = NULL; // set below
		file->fops.read  = ext2_read;
		file->fops.write = NULL;
		file->fops.close = ext2_close;
		file->fops.lseek = NULL; //ext2_lseek;
		file->fops.fstat = ext2_fstat;
		file->fops.getdents = ext2_getdents;
		list_foreach(mountpoints, it) {
			mountpoint_t *mp = (mountpoint_t *)it->data;
			if (mp->dev == dev) {
				file->mp = mp;
				break;
			}
		}
		file->path = strdup(path);
		file->count++;
		assert(file->count == 1);

		assert(file->mp != NULL);

		kfree(inode);

		return fd;
	}
	else {
		destroy_filp(fd);
		return -ENOENT;
	}
}

static int ext2_close(int fd, struct open_file *file) {
	assert(fd >= 0);
	assert(file != NULL);

	if (file->data) {
		struct ext2_getdents_info *info = file->data;
		// Used by getdents
		if (info->dir_buf) {
			kfree(info->dir_buf);
			info->dir_buf = NULL;
		}
		kfree(file->data);
		file->data = NULL;
	}

	return 0;
}

static int ext2_closedir(DIR *dir) {
	assert(dir != NULL);

	if (dir->buf) {
		kfree(dir->buf);
		dir->buf = NULL;
	}

	close(dir->fd);

	memset(dir, 0, sizeof(DIR));
	kfree(dir);

	return 0;
}

static struct dirent *ext2_readdir(DIR *dir) {
	assert(dir != NULL);
	assert(dir->fd >= 0);

	if (dir->buf == NULL) {
		// This is the first invocation, so let's allocate a buffer.
		dir->_buflen = 2048;
		dir->buf = kmalloc(dir->_buflen);
	}

	if (dir->len == 0 || dir->pos >= dir->len) {
		assert(dir->pos == dir->len); // pos may not be GREATER than len, or else something is very wrong

		// Fetch some directory entries.
		dir->len = getdents(dir->fd, dir->buf, dir->_buflen);
		dir->pos = 0;

		if (dir->len == 0) {
			// We're done!
			return NULL;
		}
		else if (dir->len < 0) {
			// Uh oh.
			printk("Warning: getdents() returned %d, readdir() returning NULL\n", dir->len);
			return NULL;
		}
	}

	// OK, we should be ready to process now.
	assert(dir->buf != NULL);
	assert(dir->_buflen >= dir->len);
	assert(dir->pos >= 0);
	assert(dir->pos < dir->len);

	struct dirent *dent = (struct dirent *)(dir->buf + dir->pos);
	dir->pos += dent->d_reclen;

	return dent;
}

static DIR *ext2_opendir(mountpoint_t *mp, const char *path) {
	assert(mp != NULL);
	assert(path != NULL);

	// Fail if the path specified does not exist or is not a directory
	int fd = ext2_open(mp->dev, path, O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		return NULL;
	}
	struct open_file *file = get_filp(fd);

	DIR *dir = kmalloc(sizeof(DIR));
	memset(dir, 0, sizeof(DIR));

	// Some struct members are left as 0 or NULL here
	dir->dev = mp->dev;
	dir->ino = file->ino;
	dir->mp = mp;
	dir->fd = fd;
	dir->dops.readdir = ext2_readdir;
	dir->dops.closedir = ext2_closedir;

	return dir;
}

static int ext2_getdents(int fd, void *dp, int count) {
	// Fill upp /dp/ with at most /count/ bytes of struct dirents, read from the
	// open directory with descriptor /fd/. We need to keep track of where we are,
	// since the caller doesn't do that via the parameters.
	// TODO: read stuff as-needed from the file system, instead of reading EVERYTHING
	// at once. That was the easy solution as I already had the parsing in place prior
	// to deciding to use getdents() for user mode...

	if (count < 128) {
		// Buffer is too small to be useful.
		return -EINVAL;
	}

	struct open_file *file = get_filp(fd);
	assert(file->dev < MAX_DEVS);
	ext2_partition_t *part = (ext2_partition_t *)devtable[file->dev];
	assert(part != NULL);

	struct ext2_getdents_info *info = NULL;

	if (file->data == NULL) {
		// This directory was just opened, and we haven't actually fetched any entries yet! Do so.

		struct stat st;
		fstat(fd, &st);
		if (!S_ISDIR(st.st_mode))
			return -ENOTDIR;

		info = kmalloc(sizeof(struct ext2_getdents_info));
		memset(info, 0, sizeof(struct ext2_getdents_info));
		file->data = info;

		info->dir_buf = ext2_read_file(part, st.st_ino, &info->len);
		assert(info->dir_buf != NULL);
		assert(info->len > 0);
	}

	memset(dp, 0, count);

	assert(file->data != NULL);
	if (info == NULL)
		info = file->data;

	if (info->len != 0 && info->pos >= info->len) {
		// We're done!
		return 0;
	}

	assert(info->len > info->pos);

	int written = 0;
	while (info->pos < info->len) {
		assert(info->dir_buf != 0);
		assert(info->pos < info->len);
		ext2_direntry_t *dir = (ext2_direntry_t *)(info->dir_buf + info->pos);
		assert((char *)dir >= info->dir_buf);
		assert((char *)dir < info->dir_buf + info->len);

		// First of all, find the first valid directory entry on disk.
		// The very FIRST one should be valid, but this is run in a loop,
		// so we might stumble onto an unused entry here in a later iteration.

		while (dir->inode == 0) {
			if (info->pos >= info->len) {
				// We're done!
				return written;
			}

			assert(info->pos % part->blocksize == 0);
			info->pos += part->blocksize;
			if (info->pos >= info->len) {
				return written;
			}
			dir = (ext2_direntry_t *)( (char *)info->dir_buf + info->pos);

			continue;
		}

		// Once we get here, there must be data present.
		assert(info->pos < info->len);
		assert(dir->inode != 0);
		assert(dir->name_len != 0);
		assert(dir->rec_len > 8);

		struct dirent *dent = (struct dirent *)((char *)dp + written);

		// The name may not be as long as it can be, so the struct may be smaller than its maximum possible size.
		// We subtract MAXNAMLEN to get the size of the *rest* of the struct,
		// then add back the length of the actual name, plus 1 byte for NULL termination.
		int entry_len = sizeof(struct dirent) - MAXNAMLEN + dir->name_len + 1;

		int tmp = written + entry_len;
		if ((written + entry_len) & 3) {
			// This needs alignment
			entry_len &= ~3;
			entry_len += 4;
		}
		assert(written + entry_len - tmp <= 3);
		assert(((written + entry_len) & 3) == 0);

		if (written + entry_len > count) {
			// This entry won't fit this time around!

			if (entry_len > count) {
				// Nor will it fit *next* time around (result buffer is too short).
				// The Linux syscall returns EINVAL for this case, so I will do the same, to keep compatibility.
				return -EINVAL;
			}

			// OK, so it *will* fit next time around.
			return written;
		}

		// This entry should fit right now.
		assert((char *)dp + written + entry_len <= (char *)dp + count);

		dent->d_ino = dir->inode;
		dent->d_dev = file->dev;
		dent->d_reclen = entry_len;
		dent->d_type = DT_UNKNOWN; // Set below, if we do know the type
		dent->d_namlen = dir->name_len;
		memcpy(dent->d_name, dir->name, dir->name_len);

		switch (dir->file_type) {
			case EXT2_FT_REG_FILE:
				dent->d_type = DT_REG;
				break;
			case EXT2_FT_DIR:
				dent->d_type = DT_DIR;
				break;
			case EXT2_FT_CHRDEV:
				dent->d_type = DT_CHR;
				break;
			case EXT2_FT_BLKDEV:
				dent->d_type = DT_BLK;
				break;
			case EXT2_FT_FIFO:
				dent->d_type = DT_FIFO;
				break;
			case EXT2_FT_SOCK:
				dent->d_type = DT_SOCK;
				break;
			case EXT2_FT_SYMLINK:
				dent->d_type = DT_LNK;
				break;
		}

		info->pos += dir->rec_len;
		written += dent->d_reclen;
	}

	return written;
}
