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


	// Read the root directory
	// TODO: remove this

	uint32 bgrp = bgrp_for_inode(part_info, EXT2_ROOT_INO);
	uint32 index = local_index_for_inode(part_info, EXT2_ROOT_INO);
	printk("root dir bgrp = %d, index into inode table = %d\n", bgrp, index);

	bgd = part_info->bgdt + bgrp;
	uint32 inode_table_block = bgd->bg_inode_table;
	printk("first block of inode table = %u\n", inode_table_block);

	// byte offset into the inode table
	uint32 offset = index * sizeof(ext2_inode_t);
	uint32 block_offset = offset / part_info->blocksize;
	offset %= part_info->blocksize;

	assert(offset % sizeof(ext2_inode_t) == 0);

	printk("block_offset = %d blocks (from inode table start block %d), then %d bytes into that inode table\n", block_offset, inode_table_block, offset);

	char *inode_buf = kmalloc(part_info->blocksize);
	assert(ata_read(dev, block_to_abs_lba(part_info, inode_table_block + block_offset), inode_buf, part_info->blocksize / 512));

	ext2_inode_t *inode = (ext2_inode_t *)(inode_buf + offset);
	inode=inode;


	// TODO: take care to read ALL blocks that contain directory information, not just the first!

	uint32 dir_block = inode->i_direct[0];
	char *dir_buf = kmalloc(part_info->blocksize);
	assert(ata_read(dev, block_to_abs_lba(part_info, dir_block), dir_buf, part_info->blocksize / 512));


	ext2_direntry_t *dir = (ext2_direntry_t *)dir_buf;

	uint32 i = 0;
	do {
		char name[256] = {0};
		memcpy(name, dir->name, dir->name_len);
		printk("inode %u for %s (rec_len %u, name_len %u), type: %u\n", dir->inode, name, dir->rec_len, dir->name_len, dir->file_type);
		dir = (ext2_direntry_t *)((char *)dir + dir->rec_len);
		i += dir->rec_len; // TODO: is this and the condition below correct?
	} while(i < part_info->blocksize && dir->inode != 0);

	//
	// TODO: using inode table + index into it, calculate which block to read + the byte offset to place the ext_inode_t * at
	//

	kfree(inode_buf);
	// End read root directory
	// TODO: remove this


	return true;
}
