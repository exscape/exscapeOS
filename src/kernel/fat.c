#include <types.h>
#include <kernel/fat.h>
#include <kernel/kernutil.h>
#include <kernel/console.h>
#include <kernel/kheap.h>
#include <string.h>

bool fat_detect(ata_device_t *dev, uint8 part) {
	/* Quite a few sanity checks */
	assert(dev != NULL);
	assert(dev->exists);
	assert(!dev->is_atapi);
	assert(part <= 3);
	assert(dev->partition[part].exists);
	assert(dev->partition[part].type == PART_FAT32 || 
	       dev->partition[part].type == PART_FAT32_LBA);

	uint8 *buf = kmalloc(512);
	/* Read the Volume ID sector */
	assert(ata_read(dev, dev->partition[part].start_lba, buf));

	assert( *( (uint16 *)(buf + 510) ) == 0xAA55);

#if 0
	/* Describes the FAT BPB (BIOS Parameter Block) */
	struct fat32_bpb {
		uint32 jmp : 24; /* x86 assembly to jump past the following data */
		char oem_ident[8];
		uint16 bytes_per_sector;
		uint8 sect_per_cluster;
		uint16 reserved_sectors;
		uint8 num_fats;
		uint16 num_direntries;
		uint16 small_total_sectors; /* 0 if >65535 sectors, i.e. for all FAT32 partitions? see below */
		uint8 media_descr_type; /* 0xf8 == hard disk */
		uint16 ignore; /* FAT12/FAT16 only */
		uint16 sect_per_track;
		uint16 heads;
		uint32 hidden_sectors; /* relative LBA */
		uint32 total_sectors; /* used if >65535 sectors, i.e. all FAT32 partitions? */
	} __attribute__((packed));
	typedef struct fat32_bpb fat32_bpb_t;

	/* Describes the FAT32 EBPB, located just after the BPB (above).
	 * Note that if this struct is mapped onto a partition that is actually
	 * FAT12/FAT16, the values will be wildly incorrect!
	 */
	struct fat32_ebpb {
		uint32 sectors_per_fat; /* FAT size, in sectors */
		uint16 flags;
		uint8 fat_major_version;
		uint8 fat_minor_version; /* these two MAY be swapped */
		uint32 root_cluster_num;
		uint16 fsinfo_cluster_num;
		uint16 backup_bs_cluster_num;
		uint8 reserved[12]; /* should be all 0 */
		uint8 drive_number;
		uint8 reserved2; /* "Flags in Windows NT. Reserved otherwise. */
		uint8 signature; /* 0x28 or 0x29 */
		uint32 volumeid_serial;
		char volume_label[11]; /* space padded */
		//char sys_id_string[8]; /* always "FAT32   ". Don't use. */
	} __attribute__((packed));
	typedef struct fat32_ebpb fat32_ebpb_t;
#endif

	fat32_bpb_t *bpb = (fat32_bpb_t *)buf;

	/* Located just after the struct above on disk */
	fat32_ebpb_t *ebpb = (fat32_ebpb_t *) ( buf + sizeof(fat32_bpb_t) );

	assert(ebpb->signature == 0x28 || ebpb->signature == 0x29);

	bpb=bpb; /* silence warning */

	return true;
}

//fat_detect(&devices[disk], part);
