#ifndef _FAT_H
#define _FAT_H

#include <types.h>
#include <kernel/ata.h>

bool fat_detect(ata_device_t *dev, uint8 part);

/* Describes the FAT BPB (BIOS Parameter Block); shared between FAT12/FAT16/FAT32 */
struct fat32_bpb {
	uint8 jmp[3]; /* x86 code to jump past the following data */
	char oem_ident[8];
	uint16 bytes_per_sector;
	uint8 sectors_per_cluster;
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
	/*} __attribute__((packed));*/
	/*typedef struct fat32_bpb fat32_bpb_t;*/

/* Describes the FAT32 EBPB, located just after the BPB (above).
 * Note that if this struct is mapped onto a partition that is actually
 * FAT12/FAT16, the values will be wildly incorrect!
 */
	/*struct fat32_ebpb {*/
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
typedef struct fat32_bpb fat32_bpb_t;
/*typedef struct fat32_ebpb fat32_ebpb_t;*/

#endif
