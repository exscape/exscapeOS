#ifndef _PART_H
#define _PART_H

#include <types.h>
#include <kernel/ata.h>

void parse_mbr(ata_device_t *dev);

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

#endif
