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

int ext2_open(uint32 dev, const char *path, int mode);
int ext2_getdents(int fd, void *dp, int count);
int ext2_stat(mountpoint_t *mp, const char *path, struct stat *st);

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

bool ext2_read_inode(ext2_partition_t *part, uint32 inode, void *buf) {
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

	char inode_buf[512] = {0};
	assert(ata_read(part->dev, block_to_abs_lba(part, inode_table_block + block_offset) + offset/512, inode_buf, 1));

	memcpy(buf, inode_buf + (offset % 512), sizeof(ext2_inode_t));

	return true; // TODO: make function void, or fix error handling
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
		assert(ata_read(part->dev, block_to_abs_lba(part, blocklist[i]), (char *)buf + i * part->blocksize, part->blocksize / 512));
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
	assert(ata_read(part->dev, block_to_abs_lba(part, singly_block), blocklist, part->blocksize / 512));

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
	assert(ata_read(part->dev, block_to_abs_lba(part, doubly_block), singly_blocks, part->blocksize / 512));

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
	assert(ata_read(part->dev, block_to_abs_lba(part, triply_block), doubly_blocks, part->blocksize / 512));

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

uint16 internet_checksum(void *ptr, uint32 length);

// Test function, used for the early development (long before VFS integration) only.
void ext2_lsdir(ext2_partition_t *part, uint32 inode_num) {
	assert(part != NULL);
	assert(inode_num >= EXT2_ROOT_INO);

	ext2_inode_t *inode = kmalloc(sizeof(ext2_inode_t));
	ext2_read_inode(part, inode_num, inode);

	if ((inode->i_mode & EXT2_S_IFDIR) == 0) {
		printk("warning, inode %u is not a directory! ignoring.\n");
		kfree(inode);
		return;
	}

	uint32 num_blocks = inode->i_blocks/(2 << part->super.s_log_block_size);
	kfree(inode); inode = NULL;
	assert(num_blocks > 0);

	ext2_direntry_t *dir = (ext2_direntry_t *)ext2_read_file(part, inode_num, NULL);
	ext2_direntry_t *orig_ptr = dir; // required for kfree, as we modify dir() below, and thus can't pass it to kfree

	uint32 i = 0;
	uint32 num = 0;
	do {
		if (i < num_blocks * part->blocksize && dir->inode == 0) {
			assert(i % part->blocksize == 0);
			printk("\n\ndir->inode == 0 at i = %u, skipping ahead 1 block\n\n\n", i);
			dir = (ext2_direntry_t *)( (char *)dir + part->blocksize);
			i += part->blocksize;
			continue;
		}
		else if (i >= num_blocks * part->blocksize) {
			printk("reading past directory, exiting!\n");
			break;
		}

		char name[256] = {0};

		if (dir->name_len < 120) {
			memcpy(name, dir->name, dir->name_len);
		}
		else {
			memcpy(name, dir->name + 120, dir->name_len - 120);
		}

		printk("inode %u for %s (rec_len %u, name_len %u), type: %u\n", dir->inode, name, dir->rec_len, dir->name_len, dir->file_type);
		i += dir->rec_len; // TODO: is this and the condition below correct?
		dir = (ext2_direntry_t *)((char *)dir + dir->rec_len);
		num++;
	} while(i < num_blocks * part->blocksize);

	printk("printed %u entries, %u bytes of records (out of %u read)\n", num, i, num_blocks * part->blocksize);

	kfree(orig_ptr);
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
	assert(ata_read(dev, dev->partition[part].start_lba + 2, buf, 2));

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

	// TODO: Fix possible off-by-one errors
	uint32 num_bgdt_sectors = 1 + ((part_info->super.s_blocks_count / part_info->super.s_blocks_per_group + 1) * sizeof(ext2_bgd_t)) / 512;
	ext2_bgd_t *bgd = kmalloc(num_bgdt_sectors * 512);
	assert(ata_read(dev, block_to_abs_lba(part_info, part_info->super.s_log_block_size == 0 ? 2 : 1), bgd, num_bgdt_sectors));
	part_info->bgdt = bgd; // Array of block group descriptors

	// Set up the mountpoint
	mountpoint_t *mp = kmalloc(sizeof(mountpoint_t));
	memset(mp, 0, sizeof(mountpoint_t));
	mp->path[0] = 0; // not set up here
	mp->mpops.open     = ext2_open;
//	mp->mpops.opendir  = ext2_opendir;
	mp->mpops.stat     = ext2_stat;
	mp->dev = next_dev; // increased below
	list_append(mountpoints, mp);

	// Store this in the device table (used for dev ID number -> partition mappings)
	devtable[next_dev++] = (void *)part_info;

	/* Add the new partition entry to the list */
	list_append(ext2_partitions, part_info);

	ext2_lsdir(part_info, EXT2_ROOT_INO); // TODO: remove this

	return true;
}

// Find the inode number for a given path, by looking recursively, starting at,
// with help from inode_for_path(), the root directory.
static uint32 _inode_for_path(ext2_partition_t *part, const char *path, uint32 parent_inode) {
	assert(part != NULL);
	assert(path != NULL);
	printk("in _inode_for_path; path = %s, parent_inode = %u\n", path, parent_inode);

	assert(strlen(path) <= 255 || strchr(path, '/') - path <= 255);
	char cur_entry[256] = {0};
	strlcpy(cur_entry, path, 256);

	if (strchr(cur_entry, '/'))
		*strchr(cur_entry, '/') = 0;

	printk("cur_entry = %s\n", cur_entry);

	ext2_inode_t *inode = kmalloc(sizeof(ext2_inode_t));
	ext2_read_inode(part, parent_inode, inode);

	if ((inode->i_mode & EXT2_S_IFDIR) == 0) {
		printk("Invalid path specified to inode_for_path! Inode %u is not a directory, but the requested file is supposed to be a subdirectory!\n", parent_inode);
		panic("Non-directory encountered in path");
	}

	kfree(inode); inode = NULL;

	uint32 size;
	ext2_direntry_t *dir = (ext2_direntry_t *)ext2_read_file(part, parent_inode, &size);
	ext2_direntry_t *orig_ptr = dir; // required for kfree, as we modify dir() below, and thus can't pass it to kfree

	uint32 i = 0;
	uint32 num = 0;
	do {
		if (i < size && dir->inode == 0) {
			assert(i % part->blocksize == 0);
			printk("\n\ndir->inode == 0 at i = %u, skipping ahead 1 block\n\n\n", i);
			dir = (ext2_direntry_t *)( (char *)dir + part->blocksize);
			i += part->blocksize;
			continue;
		}
		else if (i >= size) {
			printk("reading past directory, exiting!\n");
			break;
		}

		if (strlen(cur_entry) == dir->name_len && memcmp(cur_entry, dir->name, dir->name_len) == 0) {
			// Found it!
			printk("found it! inode %u; path=%s, cur_entry==name==%s\n", dir->inode, path, cur_entry);

			if (strcmp(path, cur_entry) == 0) {
				// Found the actual file!
				kfree(orig_ptr);
				return dir->inode;
			}
			else if (strchr(path, '/')) {
				kfree(orig_ptr);
				return _inode_for_path(part, strchr(path, '/') + 1, dir->inode);
			}
			else
				panic("end of path, but file not found");
		}

		i += dir->rec_len; // TODO: is this and the condition below correct?
		dir = (ext2_direntry_t *)((char *)dir + dir->rec_len);
		num++;
	} while(i < size);

	kfree(orig_ptr);

	printk("reached the end of _inode_for_path(%s) in dir with inode %u, returning 0 (not found)\n", path, parent_inode);
	return 0;
}

static uint32 inode_for_path(ext2_partition_t *part, const char *path) {
	assert(part != NULL);
	assert(path != NULL);
	printk("in inode_for_path; path = %s\n", path);

	if (strcmp(path, "/") == 0)
		return EXT2_ROOT_INO;
	else if (*path == '/') {
		return _inode_for_path(part, path + 1, EXT2_ROOT_INO);
	}
	else
		panic("Invalid path in inode_for_path");
}

int ext2_stat(mountpoint_t *mp, const char *path, struct stat *st) {
	assert(mp != NULL);
	assert(path != NULL);
	assert(st != NULL);

	ext2_partition_t *part = (ext2_partition_t *)devtable[mp->dev];
	assert(part != NULL);

	uint32 inode_num = inode_for_path(part, path);
	if (inode_num == 0)
		return -ENOENT; // TODO: this is the correct errno, right?

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
	st->st_mode = inode->i_mode; // TODO: are the flags correct, or is some sort of conversion required?
	st->st_nlink = inode->i_links_count;
	st->st_size = (inode->i_mode & EXT2_S_IFDIR) ? 0 : inode->i_size;
	st->st_mtime = inode->i_mtime;
	st->st_ctime = inode->i_ctime;
	st->st_atime = inode->i_atime;
	st->st_blksize = part->blocksize;
	st->st_blocks = inode->i_blocks;

	kfree(inode);

	return 0;
}

// Same as used for FAT. I mean, it works, so... eh.
// TODO: rewrite this to use file->inode instead (and perhaps make stat() use that code, too)
int ext2_fstat(int fd, struct stat *buf) {
    struct open_file *file = get_filp(fd);

    char relpath[PATH_MAX+1] = {0};
    find_relpath(file->path, relpath, NULL);

    return ext2_stat(file->mp, relpath, buf);
}

int ext2_open(uint32 dev, const char *path, int mode) {
	printk("ext2_open(dev=%u, path=%s, mode=%u)\n", dev, path, mode);
	assert(dev <= MAX_DEVS - 1);
	assert(devtable[dev] != NULL);
	assert(path != NULL);
	mode=mode; // still unused

	int fd;
	struct open_file *file = new_filp(&fd);
	if (!file || fd < 0)
		return -EMFILE;

	ext2_partition_t *part = (ext2_partition_t *)devtable[dev];
	assert(part != NULL);

	uint32 inode_num = inode_for_path(part, path);

	if (inode_num < EXT2_ROOT_INO) {
		destroy_filp(fd);
		return -ENOENT;
	}

	ext2_inode_t *inode = kmalloc(sizeof(ext2_inode_t));
	ext2_read_inode(part, inode_num, inode);

	printk("inode = %u returned to ext2_open for path %s\n", inode_num, path);
	printk("file mode = 0x%x, size = %u, 512-byte blocks = %u\n", inode->i_mode, inode->i_size, inode->i_blocks);

	if (inode_num >= EXT2_ROOT_INO) {
		file->dev = dev;
		file->ino = inode_num;
		file->_cur_ino = inode_num; // TODO: this won't do for ext2
		file->offset = 0;
		file->size = inode->i_size;
		file->mp = NULL; // set below
		file->fops.read  = NULL; //ext2_read;
		file->fops.write = NULL;
		file->fops.close = NULL; //ext2_close;
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

		return fd;
	}
	else {
		destroy_filp(fd);
		return -ENOENT;
	}
}

int ext2_close(int fd, struct open_file *file) {
	printk("TODO: implement ext2_close()\n");

	return 0;
}

struct ext2_getdents_info {
	char *dir_buf;
	uint32 len;
	uint32 pos;
};

int ext2_getdents(int fd, void *dp, int count) {
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

		// TODO: free this in close(); how, though? Can we just free ->data if not NULL?
		// Look through other code to find out.
		info = kmalloc(sizeof(struct ext2_getdents_info));
		memset(info, 0, sizeof(struct ext2_getdents_info));
		file->data = info;

		info->dir_buf = ext2_read_file(part, st.st_ino, &info->len);
		printk("read directory contents from disk; %u bytes\n", info->len);
		assert(info->dir_buf != NULL);
		assert(info->len > 0);
	}

	memset(dp, 0, count);

	assert(file->data != NULL);

	if (info == NULL)
		info = file->data;

	//
	// TODO: does this exit condition work with ext2? there way be empty entries at the end!
	//
	if (info->len != 0 && info->pos >= info->len) {
		// We're done!
		// TODO: cleanup?
		return 0;
	}

	assert(info->len > info->pos);
//	assert((info->pos & 3) == 0);

	int written = 0;
	while (info->pos < info->len) {
		ext2_direntry_t *dir = (ext2_direntry_t *)(info->dir_buf + info->pos);

		// First of all, find the first valid directory entry on disk.
		// The very FIRST one should be valid, but this is run in a loop,
		// so we might stumble onto an unused entry here in a later iteration.

		while (dir->inode == 0) {
			if (info->pos >= info->len) {
				// We're done!
				// TODO: cleanup!
				return written;
			}

			assert(info->pos % part->blocksize == 0);
			//printk("\n\ngetdents(): dir->inode == 0 at position %u (len = %u), skipping ahead 1 block\n\n\n", info->pos, info->len);
			info->pos += part->blocksize;
			dir = (ext2_direntry_t *)( (char *)dir + info->pos);

			continue;
		}

		// Once we get here, there must be data present.
		assert(info->pos < info->len);
		assert(dir->inode != 0);
		assert(dir->name_len != 0);
		assert(dir->rec_len > 8);

		struct dirent *dent = (struct dirent *)((char *)dp + written);
		//printk("dir=0x%p, dent=0x%p, written = %u\n", dir, dent, written);

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

		switch (dir->file_type & EXT2_S_IFMT) {
			case EXT2_S_IFSOCK:
				dent->d_type = EXT2_FT_SOCK;
				break;
			case EXT2_S_IFLNK:
				dent->d_type = EXT2_FT_SYMLINK;
				break;
			case EXT2_S_IFREG:
				dent->d_type = EXT2_FT_REG_FILE;
				break;
			case EXT2_S_IFBLK:
				dent->d_type = EXT2_FT_BLKDEV;
				break;
			case EXT2_S_IFDIR:
				dent->d_type = EXT2_FT_DIR;
				break;
			case EXT2_S_IFCHR:
				dent->d_type = EXT2_FT_CHRDEV;
				break;
			case EXT2_S_IFIFO:
				dent->d_type = EXT2_FT_FIFO;
				break;
		}

		info->pos += dir->rec_len;
		written += dent->d_reclen;

		//printk("set info->pos=%u (len = %u) and written=%u (count = %u) before new loop\n", info->pos, info->len, written, count);

		// TODO: should the struct dirents be DWORD aligned?


	}

/*
		info->pos += dir->d_reclen;
		assert((info->pos & 3) == 0);
	}

	// If we get here, the loop exited due to there being no more entries,
	// though the caller can receive more still. Ensure the state is consistent,
	// and return.
	assert(written != 0); // we shouldn't get here if nothing was written
	assert(info->pos == info->len);
*/
	return written;
}
