#ifndef _ATA_H
#define _ATA_H

#include <types.h>

void ata_init(void);

typedef struct ata_channel {
	uint16 base; /* IO base address */
	uint16 ctrl; /* Control reg base */
	uint16 bmide; /* Bus master IDE */
	uint8 nIEN; /* the ATA nIEN flag (disables interrupts) */
} ata_channel_t;

typedef struct ata_device {
	bool exists;
	uint8 channel; /* 0 (Primary) or 1 (Secondary) */
	uint8 drive; /* 0 (Master) or 1 (Slave) */
	uint16 capabilities;
	uint64 size; /* size in sectors */
	unsigned char ident[64]; /* model + serial number */
} ata_device_t;

#define ATA_REG_BASE_PRI 0x1f0
#define ATA_REG_BASE_SEC 0x170

/* Offsets from the base addresses above */
#define ATA_REG_DATA 0
#define ATA_REG_FEATURES_ERROR 1 /* is this used for non-ATAPI? */
#define ATA_REG_SECTOR_COUNT 2
#define ATA_REG_LBA_LO 3 /* also sector number */
#define ATA_REG_LBA_MID 4 /* also cylinder low */
#define ATA_REG_LBA_HI 5 /* also cylinder high */
#define ATA_REG_DRIVE_SELECT 6 /* also head */
#define ATA_REG_COMMAND 7
#define ATA_REG_STATUS 7

#define ATA_REG_DEV_CONTROL 0xffff /* Used internally */
#define ATA_REG_DEV_CONTROL_PRI 0x3f6 /* device control / alt. status reg */
#define ATA_REG_DEV_CONTROL_SEC 0x376

/* The three bits that we're allowed to touch in the device control register */
#define ATA_REG_DEV_CONTROL_NIEN (1 << 1) /* interrupts disabled if 1 */
#define ATA_REG_DEV_CONTROL_SRST (1 << 2) /* Software reset bit */
#define ATA_REG_DEV_CONTROL_HOB (1 << 3) /* High Order Bit, used for LBA48 */

/* ATA commands we use */
#define ATA_CMD_IDENTIFY 0xec

/* Drive IDs to be sent to the drive select IO port */
#define ATA_MASTER 0xa0
#define ATA_SLAVE 0xb0

/* Used internally (not defined in the ATA standard) */
#define ATA_PRIMARY 0
#define ATA_SECONDARY 1

/* ATA status register flags */
#define ATA_SR_BSY 0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF 0x20
#define ATA_SR_DSC 0x10
#define ATA_SR_DRQ 0x08
/*#define ATA_SR_CORR 0x04*/ /* marked as obsolete in ATA-ATAPI 6 */
/*#define ATA_SR_IDX 0x02*/ /* same as above */
#define ATA_SR_ERR 0x01

#endif
