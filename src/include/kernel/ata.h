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
	bool is_atapi; /* false: regular ATA. true: ATAPI (mostly optical drives, these days) */
	uint32 capabilities;
	uint64 size; /* size in sectors */
	char model[41];
	char serial[21];
	uint8 ata_ver; /* the ATA version this disk conforms to */
	uint8 max_udma_mode; /* 0 through 5; other values are invalid */
} ata_device_t;

#define ATA_REG_BASE_PRI 0x1f0
#define ATA_REG_BASE_SEC 0x170

/* Offsets from the base addresses above */
#define ATA_REG_DATA 0
#define ATA_REG_ERROR 1
#define ATA_REG_FEATURES 1 /* features on write; is this used for non-ATAPI? */
#define ATA_REG_SECTOR_COUNT 2
#define ATA_REG_LBA_LO 3 /* also sector number */
#define ATA_REG_LBA_MID 4 /* also cylinder low */
#define ATA_REG_LBA_HI 5 /* also cylinder high */
#define ATA_REG_DRIVE_SELECT 6 /* also head */
#define ATA_REG_COMMAND 7 /* acts as command on write... */
#define ATA_REG_STATUS 7 /* ... and status on read */

#define ATA_REG_DEV_CONTROL 0xffff /* Used internally */
#define ATA_REG_DEV_ALT_STATUS 0xffff /* Used internally */
#define ATA_REG_DEV_CONTROL_PRI 0x3f6 /* device control / alt. status reg */
#define ATA_REG_DEV_CONTROL_SEC 0x376

/* The three bits that we're allowed to touch in the device control register */
#define ATA_REG_DEV_CONTROL_NIEN (1 << 1) /* interrupts disabled if 1 */
#define ATA_REG_DEV_CONTROL_SRST (1 << 2) /* Software reset bit */
#define ATA_REG_DEV_CONTROL_HOB (1 << 3) /* High Order Bit, used for LBA48 */

/* ATA commands we use */
#define ATA_CMD_IDENTIFY 0xec

/* Drive IDs to be sent to the drive select IO port */
#define ATA_DRIVE 0xa0 /* base command */
#define ATA_MASTER 0 /* usage: ATA_DRIVE | (ATA_MASTER << 4) */
#define ATA_SLAVE 1 /* usage: ATA_DRIVE | (ATA_SLAVE << 4) */

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

/* The flags in the error register */
#define    ATA_ER_BBK      0x80
#define    ATA_ER_UNC      0x40
#define    ATA_ER_MC      0x20
#define    ATA_ER_IDNF      0x10
#define    ATA_ER_MCR      0x08
#define    ATA_ER_ABRT      0x04
#define    ATA_ER_TK0NF   0x02
#define    ATA_ER_AMNF      0x01

/* Flags used in the ata_device_t capabilities field - not defined in any standard */
#define ATA_CAPABILITY_LBA28 (1 << 0)
#define ATA_CAPABILITY_LBA48 (1 << 1)
#define ATA_CAPABILITY_WRITE_CACHE (1 << 2)
#define ATA_CAPABILITY_FLUSH_CACHE (1 << 3)

#endif
