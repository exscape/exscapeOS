#include <sys/types.h>
#include <kernel/ext2.h>
#include <kernel/kernutil.h>
#include <kernel/console.h>
#include <kernel/heap.h>
#include <kernel/list.h>
#include <string.h>
#include <kernel/partition.h>
#include <kernel/vfs.h>
#include <path.h>
#include <sys/errno.h>
#include <kernel/pmm.h>
#include <kernel/time.h>

list_t *ext2_partitions = NULL;

#define min(a,b) ( (a < b ? a : b) )

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
	//printk("[reading inode %u] inode bgrp = %d, index into inode table = %d\n", inode, bgrp, index);

	ext2_bgd_t *bgd = part->bgdt + bgrp;
	uint32 inode_table_block = bgd->bg_inode_table;
	//printk("[reading inode %u] first block of inode table = %u\n", inode, inode_table_block);

	// byte offset into the inode table
	uint32 offset = index * sizeof(ext2_inode_t);
	uint32 block_offset = offset / part->blocksize;
	offset %= part->blocksize;

	assert(offset % sizeof(ext2_inode_t) == 0);

	//printk("[reading inode %u] block_offset = %d blocks (from inode table start block %d), then %d bytes into that inode table\n", inode, block_offset, inode_table_block, offset);

	char inode_buf[512] = {0};
	assert(ata_read(part->dev, block_to_abs_lba(part, inode_table_block + block_offset) + offset/512, inode_buf, 1));

	memcpy(buf, inode_buf + (offset % 512), sizeof(ext2_inode_t));

	return true; // TODO: make function void, or fix error handling
}

static void	read_indirect_blocks(ext2_partition_t *part, uint32 indir_block, uint32 max_num, void *buf) {
	assert(part != NULL);
	assert(indir_block > EXT2_ROOT_INO);
	assert(max_num <= part->blocksize / 4);
	assert(buf != NULL);
	
	// To begin with, we read the contents of the indirect block into a buffer;
	// this is really just an array of uint32s.
	char *local_buf = kmalloc(part->blocksize);
	assert(ata_read(part->dev, block_to_abs_lba(part, indir_block), local_buf, part->blocksize / 512));

	// Next, for each block number in the array, read the data in that block into the buffer,
	// assuming we haven't reached the end yet.
	uint32 *blocks = (uint32 *)local_buf;
	for (uint32 i = 0; i < max_num; i++) {
		assert(ata_read(part->dev, block_to_abs_lba(part, *blocks++), (char *)buf + i * part->blocksize, part->blocksize / 512));
	}

	kfree(local_buf);
}

// Test function, used for the early development (long before VFS integration) only.
void ext2_lsdir(ext2_partition_t *part, uint32 inode_num) {
	assert(part != NULL);
	assert(inode_num >= EXT2_ROOT_INO);

	ext2_inode_t *inode = kmalloc(sizeof(ext2_inode_t));
	ext2_read_inode(part, inode_num, inode);

	// Calculate number of FS blocks used by this directory; the i_blocks value
	// is in 512-byte sectors, not blocks
	uint32 num_blocks = inode->i_blocks/(2 << part->super.s_log_block_size);
	assert(num_blocks > 0);

	char *dir_buf = kmalloc(num_blocks * part->blocksize);
	printk("allocated %u bytes for the directory buffer (%u blocks)\n", num_blocks * part->blocksize, num_blocks);
	memset(dir_buf, 0, num_blocks * part->blocksize);

	uint32 read_blocks = 0; 
	
	for (read_blocks = 0; read_blocks < min(12, num_blocks); read_blocks++) {
		printk("reading direct block %u, LBA %u\n", inode->i_direct[read_blocks], block_to_abs_lba(part, inode->i_direct[read_blocks]));
		// Use direct blocks; there are only 12, though
		assert(ata_read(part->dev, block_to_abs_lba(part, inode->i_direct[read_blocks]), dir_buf + read_blocks * part->blocksize, part->blocksize / 512));

		for (uint32 i=0; i < part->blocksize; i++) {
			printk("%c ", *(dir_buf + read_blocks * part->blocksize + i));
		}

		printk("\n\nnext block\n");
	}

	if (num_blocks > read_blocks) {
		// The 12 direct blocks weren't enough, so we'll have to use singly indirect ones.
		uint32 num_indir_blocks = min(num_blocks - read_blocks, part->blocksize/4);
		read_indirect_blocks(part, inode->i_singly, num_indir_blocks, dir_buf + read_blocks * part->blocksize);
		read_blocks += num_indir_blocks;
	}

	if (num_blocks > read_blocks) {
		// The 12 direct + (blocksize/4) singly indirect ones weren't enough, either!
		printk("%d blocks read, but %d required! using doubly indirect\n", read_blocks, num_blocks);

		// First, read the ARRAY of of SINGLY indirect blocks from disk.
		// The block pointer is stored in the doubly indirect entry of the inode (inode->i_doubly).
		char *blocklist = kmalloc(part->blocksize);
		uint32 *singly_blocks = (uint32 *)blocklist;
		assert(ata_read(part->dev, block_to_abs_lba(part, inode->i_doubly), blocklist, part->blocksize / 512));

		// Next, read through as many of these singly indirect blocks as required.
		for (uint32 i = 0; num_blocks > read_blocks && i < part->blocksize/4 && *singly_blocks != 0; i++) {
			uint32 singly = *singly_blocks;
			uint32 num_indir_blocks = min(num_blocks - read_blocks, part->blocksize/4); // how many data blocks to read from THIS singly indir block

			read_indirect_blocks(part, singly, num_indir_blocks, dir_buf + read_blocks * part->blocksize);
			read_blocks += num_indir_blocks;
		}
		kfree(blocklist);
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
		panic("TODO: support triply indirect blocks for directories");
	}

	ext2_direntry_t *dir = (ext2_direntry_t *)dir_buf;

	uint32 i = 0;
	uint32 num = 0;
	do {
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
//		  	printk("old_dir = 0x%08x\n", old_dir);	
//		}
		num++;
//		printk("i=%u read_blocks*blsz = %u, dir->inode = %u\n", i, read_blocks * part->blocksize, dir->inode);
	} while(i < read_blocks * part->blocksize && dir->inode != 0);
	printk("printed %d entries, %d bytes of records\n", num, i);

	//
	// TODO: using inode table + index into it, calculate which block to read + the byte offset to place the ext_inode_t * at
	//

	kfree(inode);
	kfree(dir_buf);

	// End read root directory
	// TODO: remove this
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
//	mp->mpops.open     = ext2_open;
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
