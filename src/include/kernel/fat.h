#ifndef _FAT_H
#define _FAT_H

#include <sys/types.h>
#include <kernel/ata.h>
#include <kernel/vfs.h>

bool fat_detect(ata_device_t *dev, uint8 part);

#define ATTRIB_READONLY 0x1
#define ATTRIB_HIDDEN 0x2
#define ATTRIB_SYSTEM 0x4
#define ATTRIB_VOLUME_ID 0x8
#define ATTRIB_DIR 0x10
#define ATTRIB_ARCHIVE 0x20

/* Used for Long File Name entries */
#define ATTRIB_LFN (ATTRIB_READONLY | ATTRIB_HIDDEN | ATTRIB_SYSTEM | ATTRIB_VOLUME_ID)

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

/* Describes the FAT32 EBPB, located just after the BPB (above).
 * Note that if this struct is mapped onto a partition that is actually
 * FAT12/FAT16, the values will be wildly incorrect!
 */
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
} __attribute__((packed));
typedef struct fat32_bpb fat32_bpb_t;

#define FAT32_MAGIC 0x1234fedc
typedef struct fat32_partition {
	uint32 magic; // used to verify this entry in the device table
	ata_device_t *dev; /* the device that holds this partition */
	uint32 fat_start_lba; /* the LBA where the FAT begins */
	uint32 end_lba; /* last valid LBA for this partition */
	uint32 cluster_start_lba;
	uint32 sectors_per_cluster;
	uint32 root_dir_first_cluster;
	uint32 cluster_size; /* part->cluster_size is easier than part->bpb->sectors_per_cluster * 512 */

	/* TODO: better naming. This is a pointer back to the dev->partition[x] structure. */
	partition_t *part_info;

	/* The entire BPB and EBPB data structures for this partition */
	fat32_bpb_t *bpb;

	mountpoint_t *mp;

	uint8 *cached_fat;
} fat32_partition_t;

/* Time format used in fat32_direntry_t */
typedef struct fat32_time {
	uint16 second : 5;
	uint16 minute : 6;
	uint16 hour : 5;
} __attribute__((packed)) fat32_time_t;

/* Date format used in fat32_direntry_t. Relative to 1980-01-01 */
typedef struct fat32_date {
	uint16 day : 5;
	uint16 month : 4;
	uint16 year : 7;
} __attribute__((packed)) fat32_date_t;

typedef struct fat32_direntry {
	char name[11]; /* 8.3 file name; the 8 part is space padded if shorter */
	uint8 attrib;
	uint8 reserved;
	uint8 created_10ths;

	fat32_time_t create_time;
	fat32_date_t create_date;

	fat32_date_t access_date;

	uint16 high_cluster_num;

	fat32_time_t mod_time;
	fat32_date_t mod_date;

	uint16 low_cluster_num;
	uint32 file_size;
} __attribute__((packed)) fat32_direntry_t;

/* Maps on to a dir fat32_direntry_t if attrib == 0xF (ATTRIB_LFN) */
typedef uint16 UTF16_char;
typedef struct fat32_lfn {
	uint8 entry;
	UTF16_char name_1[5];
	uint8 attrib; /* Always 0xF for LFN entries */
	uint8 long_entry_type; /* should be 0 for all LFN entries */
	uint8 checksum;
	UTF16_char name_2[6];
	char zero[2]; /* always zero */
	UTF16_char name_3[2];
} __attribute__((packed)) fat32_lfn_t;

#endif
