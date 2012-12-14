#ifndef _PARTITION_H
#define _PARTITION_H

#include <sys/types.h>

typedef struct partition {
	bool exists;
	bool bootable;
	uint8 type; /* system id byte */
	uint32 start_lba;
	uint32 total_sectors;
	uint8 filesystem; /* not necessarily the same as type above */
	/* The /filesystem/ member is only slightly related to the /type/ member.
	 * More specifically, /type/ is the System ID byte from the MBR, which
	 * gives a maximum of 255 (1 through 0xff) types. All regular Linux
	 * partitions uses 0x83, for example, no matter the filesystem.
	 * 
	 * /filesystem/, on the other hand, is set depending on the exact
	 * filesystem used, e.g. ext2, ext3, FAT32, ReiserFS, XFS, ...
	 * The filesystem member should be one of the FS_* values.
	 * As of right now only FAT32 is supported, and far from completely.o
	 */
} partition_t;

#include <kernel/ata.h>

void parse_mbr(struct ata_device *dev);

/* System ID byte values */
#define PART_EXTENDED_8GB 0x05
#define PART_EXTENDED 0x0f
#define PART_FAT12 0x01
#define PART_FAT16 0x06
#define PART_FAT16_LBA 0x0e
#define PART_FAT32 0x0b
#define PART_FAT32_LBA 0x0c
#define PART_LINUX 0x83
#define PART_LINUX_SWAP 0x82

/* Filesystems we know about */
#define FS_NOT_YET_TESTED 0 /* the default value, before the FAT driver is called */
#define FS_FAT32 0x01
#define FS_UNKNOWN 0xff

#endif
