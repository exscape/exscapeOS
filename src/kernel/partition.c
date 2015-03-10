#include <sys/types.h>
#include <kernel/partition.h>
#include <kernel/kernutil.h>
#include <kernel/console.h>
#include <kernel/ata.h>
#include <string.h> /* memset */
#include <kernel/heap.h>

/* Used in this file only. */
struct mbr_ptable {
	uint8  bootable     : 8; /* bit 7 (0x80) is set if bootable */
	uint8  start_head   : 8;
	uint8  start_sector : 6;
	uint16 start_cyl    : 10;
	uint8  system_id    : 8;
	uint8  ending_head  : 8;
	uint8  ending_sector : 6;
	uint16 ending_cyl   : 10;
	uint32 rel_sector   : 32; /* also: starting LBA adress */
	uint32 total_sectors : 32;
} __attribute__((packed));

/* Reads LBA0, and sets up the partition entries for the disk. */
void parse_mbr(ata_device_t *dev) {
	assert(dev != NULL);
	assert(sizeof(struct mbr_ptable) == 16);

	memset(dev->partition, 0, sizeof(partition_t) * 4);

	/* If this device is nonexistant or an ATAPI device, set all partitions to
	 * not present. */
	if (!dev->exists || dev->is_atapi) {
		for (int i=0; i < 3; i++) {
			dev->partition[i].exists = false;
		}

		return;
	}

	/* Read the MBR from disk */
	unsigned char *buf = kmalloc(512);
	int ret = ata_read(dev, /* LBA = */ 0, buf, 1);
	assert(ret != 0);

	/* Last 2 bytes of the MBR should be 0xAA55.
	 * If they're not, set all partitions to not present and return. */
	if (*(  (uint16 *)(buf + 510)  ) != 0xAA55) {
		for (int i=0; i < 3; i++) {
			dev->partition[i].exists = false;
		}

		return;
	}

	/* Loop through the partition table. */
	for (int i = 0; i < 3; i++) {
		/* A bit ugly, but... eh. Partition entry 1 (1-indexed) is at offset 446, #2 at offset 446+16, etc. */
		struct mbr_ptable *part = ((struct mbr_ptable *)(buf + 446)) + i;

		/* Check whether this partition exists or not */
		if (part->system_id == 0 || part->total_sectors == 0) {
			dev->partition[i].exists = false;
			continue;
		}
		else
			dev->partition[i].exists = true;

		/* Set the bootable flag */
		if (part->system_id & 0x80)
			dev->partition[i].bootable = true;

		/* Copy the system ID byte and the partition location/size */
		dev->partition[i].type = part->system_id;
		dev->partition[i].start_lba = part->rel_sector;
		dev->partition[i].total_sectors = part->total_sectors;
	}
}
