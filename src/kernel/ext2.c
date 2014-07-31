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
char *ext2_read_file(ext2_partition_t *part, uint32 inode_num); // read an ENTIRE FILE and return a malloc'ed buffer with it

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

static void read_direct_blocks(ext2_partition_t *part, uint32 *blocklist, uint32 num, void *buf) {
	assert(part != NULL);
	assert(blocklist != NULL);
	assert(*blocklist != 0);
	assert(num > 0);
	assert(buf != NULL);
	printk("read_direct_blocks(num = %u)\n", num);

	// For each block number in the array, read the data in that block into the buffer,
	// assuming we haven't reached the end yet.
	for (uint32 i = 0; i < num; i++) {
	//	printk("read_direct_blocks: i = %u, *blocklist = %u\n", i, *blocklist);
		if (*blocklist==0) {
			blocklist++;
//			i++; // TODO: should this be here or not? I *think* it shouldn't
			continue;
		}
		assert(ata_read(part->dev, block_to_abs_lba(part, *blocklist++), (char *)buf + i * part->blocksize, part->blocksize / 512));
	}
}

static void read_indirect_blocks(ext2_partition_t *part, uint32 indir_block, uint32 max_num, void *buf) {
	assert(part != NULL);
	assert(indir_block > EXT2_ROOT_INO);
	assert(max_num <= part->blocksize / 4); // This may be relaxed later, in case blocklists are consecutive on disk
	assert(buf != NULL);
	
	// To begin with, we read the contents of the indirect block into a buffer;
	// this is really just an array of uint32s.
	uint32 *blocklist = kmalloc(part->blocksize);
	assert(ata_read(part->dev, block_to_abs_lba(part, indir_block), blocklist, part->blocksize / 512));

	// Next, read the blocks.
	read_direct_blocks(part, blocklist, max_num, buf);

	kfree(blocklist);
}

uint16 internet_checksum(void *ptr, uint32 length);

// Test function, used for the early development (long before VFS integration) only.
void ext2_lsdir(ext2_partition_t *part, uint32 inode_num) {
	assert(part != NULL);
	assert(inode_num >= EXT2_ROOT_INO);

	ext2_inode_t *inode = kmalloc(sizeof(ext2_inode_t));
	ext2_read_inode(part, inode_num, inode);

	if ((inode->i_mode & 0x4000) == 0) {
		printk("warning, inode %u is not a directory! ignoring.\n");
		kfree(inode);
		return;
	}

	uint32 num_blocks = inode->i_blocks/(2 << part->super.s_log_block_size);
	kfree(inode); inode = NULL;
	assert(num_blocks > 0);

	ext2_direntry_t *dir = (ext2_direntry_t *)ext2_read_file(part, inode_num);
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
		//printk("%s\n", name);

		printk("inode %u for %s (rec_len %u, name_len %u), type: %u\n", dir->inode, name, dir->rec_len, dir->name_len, dir->file_type);
		i += dir->rec_len; // TODO: is this and the condition below correct?
//		uint32 old_len = dir->rec_len;
		dir = (ext2_direntry_t *)((char *)dir + dir->rec_len);
//		if (dir->inode == 0) { 
//			ext2_direntry_t *old_dir = (ext2_direntry_t *)((char *)dir - old_len);
//			printk("old_dir = 0x%08x\n", old_dir);
//		}
		num++;
//		printk("i=%u num_blocks*blsz = %u, dir->inode = %u\n", i, num_blocks * part->blocksize, dir->inode);
	} while(i < num_blocks * part->blocksize);
	printk("printed %u entries, %u bytes of records (out of %u read)\n", num, i, num_blocks * part->blocksize);

	kfree(orig_ptr);
}

char *ext2_read_file(ext2_partition_t *part, uint32 inode_num) {
	assert(part != NULL);
	assert(inode_num >= EXT2_ROOT_INO);

	ext2_inode_t *inode = kmalloc(sizeof(ext2_inode_t));
	ext2_read_inode(part, inode_num, inode);

	uint32 num_blocks = inode->i_blocks/(2 << part->super.s_log_block_size);
	assert(num_blocks > 0);

	printk("file size = %u, i_blocks=%u, FS blocks = %u\n", inode->i_size, inode->i_blocks, num_blocks);

	char *file_data = kmalloc(num_blocks * part->blocksize);
	memset(file_data, 0, num_blocks * part->blocksize);

	uint32 read_blocks = 0;

	read_direct_blocks(part, &inode->i_direct[0], min(12, num_blocks), file_data);
	read_blocks += min(12, num_blocks);

	if (num_blocks > read_blocks) {
		// The 12 direct blocks weren't enough, so we'll have to use singly indirect ones.
		uint32 num_indir_blocks = min(num_blocks - read_blocks, part->blocksize/4);
		printk("\n\nnum_indir_blocks = %u\n\n", num_indir_blocks);
		read_indirect_blocks(part, inode->i_singly, num_indir_blocks, file_data + read_blocks * part->blocksize);
		read_blocks += num_indir_blocks;
	}

	if (num_blocks > read_blocks) {
		// The 12 direct + (blocksize/4) singly indirect ones weren't enough, either!

		// First, read the ARRAY of SINGLY indirect blocks from disk.
		// The block pointer is stored in the doubly indirect entry of the inode (inode->i_doubly).
		uint32 *singly_blocks = kmalloc(part->blocksize);
		assert(ata_read(part->dev, block_to_abs_lba(part, inode->i_doubly), singly_blocks, part->blocksize / 512));

		// Next, read through as many of these singly indirect blocks as required.
		for (uint32 i = 0; num_blocks > read_blocks && i < part->blocksize/4; i++) {
			uint32 singly = *singly_blocks++;
			assert(singly != 0);
			uint32 num_indir_blocks = min(num_blocks - read_blocks, part->blocksize/4); // how many data blocks to read from THIS singly indir block

			read_indirect_blocks(part, singly, num_indir_blocks, file_data + read_blocks * part->blocksize);
			read_blocks += num_indir_blocks;
		}
		kfree(singly_blocks);
	}

	if (num_blocks > read_blocks) {
		// THAT wasn't enough, EITHER! This likely won't happen, so I will wait to support this until required.
		// Even with a 1 kB block size, we can store 12 + 256 + 256*256 = 65804 kB this way;
		// a directory with over 64 MiB worth of data probably doesn't exist in the real world!
		// Even if each file name was 255 bytes, you could fit over 260 000 files in there.
		// Someone might do it on e.g. Linux, but I doubt anyone will on exscapeOS.
		//
		// With larger block sizes, the limits go up a lot. With 4 kB, we can store (12 + 1024 + 1024*1024) * 4 = 4 GIGABYTES
		// worth of directory data, which clearly never ever happens; who stores >16 million files per directory (each with a 255 byte name)?
		panic("TODO: support triply indirect blocks for files");
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
//	mp->mpops.stat     = ext2_stat;
	mp->dev = next_dev; // increased below
	list_append(mountpoints, mp);

	// Store this in the device table (used for dev ID number -> partition mappings)
	devtable[next_dev++] = (void *)part_info;

	/* Add the new partition entry to the list */
	list_append(ext2_partitions, part_info);

	ext2_lsdir(part_info, EXT2_ROOT_INO); // TODO: remove this

	return true;
}

int ext2_open(uint32 dev, const char *path, int mode) {
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

	//uint32 cluster = ext2_cluster_for_path(part, path, 0 /* any type */);

	//
	// TODO: find inode for path
	//
	uint32 inode = EXT2_ROOT_INO;

	if (inode <= EXT2_ROOT_INO) {
		file->dev = dev;
		file->ino = inode;
		file->_cur_ino = inode; // TODO: this won't do for ext2
		file->offset = 0;
		file->size = 0; // TODO: should this be kept or not?
		file->mp = NULL;
//		file->fops.read  = ext2_read;
		file->fops.write = NULL;
//		file->fops.close = ext2_close;
//		file->fops.lseek = ext2_lseek;
//		file->fops.fstat = ext2_fstat;
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
		assert(file->count == 1); // We have no dup, dup2 etc. yet

		assert(file->mp != NULL);

		return fd;
	}
	else {
		destroy_filp(fd);
		return -ENOENT;
	}
}

int ext2_getdents(int fd, void *dp, int count) {
	// Fill upp /dp/ with at most /count/ bytes of struct dirents, read from the
	// open directory with descriptor /fd/. We need to keep track of where we are,
	// since the caller doesn't do that via the parameters.
	// TODO: read stuff as-needed from the file system, instead of reading EVERYTHING
	// at once. That was the easy solution as I already had the parsing in place prior
	// to deciding to use getdents() for user mode...

	if (count < 128) {
		return -EINVAL;
	}

	struct open_file *file = get_filp(fd);

	if (file->data == NULL) {
		// This directory was just opened, and we haven't actually fetched any entries yet! Do so.
		assert(file->dev < MAX_DEVS);
		ext2_partition_t *part = (ext2_partition_t *)devtable[file->dev];
		assert(part != NULL);

//		struct stat st;
//		fstat(fd, &st);
//		if (!S_ISDIR(st.st_mode))
//			return -ENOTDIR;

//		file->data = ext2_opendir_cluster(part, file->ino, file->mp);
		if (file->data == NULL) {
			return -ENOTDIR; // TODO - this can't happen at the moment, though (ext2_opendir_cluster always succeds)
		}
	}

	DIR *dir = file->data;
	assert(dir != NULL);

	if (dir->len != 0 && dir->pos >= dir->len) {
		// We're done!
		assert(dir->pos == dir->len); // Anything but exactly equal is a bug
//		closedir(dir); // TODO
		file->data = NULL;
		return 0;
	}
/*
	if (dir->buf == NULL) {
		// Create the list of directory entries
		ext2_parse_dir(dir, ext2_callback_create_dentries, NULL);
	}

	if (dir->buf == NULL) {
		// If we get here, ext2_parse_dir failed
		// TODO: error reporting
		return -1;
	}
	*/

//	assert(dir->len > dir->pos);
//	assert((dir->pos & 3) == 0);
//	assert(dir->_buflen > dir->len);

	// Okay, time to get to work!
	// buffer to copy to is /dp/, and at most /count/ bytes can be written safely
	/*
	int written = 0;
	while (dir->pos < dir->len) {
		struct dirent *dent = (struct dirent *)(dir->buf + dir->pos);
		if (written + dent->d_reclen > count) {
			if (dent->d_reclen > count) {
				// Won't fit the next time around, either!
				return -EINVAL; // TODO: memory leak if we don't clean up somewhere
			}
			else {
				// This won't fit! Read it the next time around.
				return written;
			}
		}
		assert((char *)dp + written + dent->d_reclen <= (char *)dp + count);

		// Okay, this entry fits; copy it over.
		memcpy((char *)dp + written, dent, dent->d_reclen);
		written += dent->d_reclen;

		dir->pos += dent->d_reclen;
		assert((dir->pos & 3) == 0);
	}

	// If we get here, the loop exited due to there being no more entries,
	// though the caller can receive more still. Ensure the state is consistent,
	// and return.
	assert(written != 0); // we shouldn't get here if nothing was written
	assert(dir->pos == dir->len);
*/
	return 0; // TODO: written;
}
