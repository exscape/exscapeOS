#ifndef _EXT2_H
#define _EXT2_H

#include <sys/types.h>
#include <kernel/ata.h>
#include <kernel/vfs.h>

bool ext2_detect(ata_device_t *dev, uint8 part);

#define EXT2_ROOT_INO 2

/* inode i_mode flags: file types */
#define	EXT2_S_IFMT	0xF000	/* format mask  */
#define	EXT2_S_IFSOCK	0xC000	/* socket */
#define	EXT2_S_IFLNK	0xA000	/* symbolic link */
#define	EXT2_S_IFREG	0x8000	/* regular file */
#define	EXT2_S_IFBLK	0x6000	/* block device */
#define	EXT2_S_IFDIR	0x4000	/* directory */
#define	EXT2_S_IFCHR	0x2000	/* character device */
#define	EXT2_S_IFIFO	0x1000	/* fifo */

// Note that these bits are not mutually exclusive, so simply testing
// e.g. if (i_mode & EXT2_S_IFLNK) doesn't work. That will return true on
// regular files, since EXT2_S_IFLNK == EXT2_S_IFREG | EXT2_S_IFCHR!
#define EXT2_ISDIR(i_mode) ((i_mode & EXT2_S_IFDIR) == EXT2_S_IFDIR)
#define EXT2_ISLNK(i_mode) ((i_mode & EXT2_S_IFLNK) == EXT2_S_IFLNK)
#define EXT2_ISREG(i_mode) ((i_mode & EXT2_S_IFREG) == EXT2_S_IFREG)

/* inode i_mode flags: permission bits */
#define	EXT2_S_ISUID	0x0800	/* SUID */
#define	EXT2_S_ISGID	0x0400	/* SGID */
#define	EXT2_S_ISVTX	0x0200	/* sticky bit */
#define	EXT2_S_IRWXU	0x01C0	/* user access rights mask */
#define	EXT2_S_IRUSR	0x0100	/* read */
#define	EXT2_S_IWUSR	0x0080	/* write */
#define	EXT2_S_IXUSR	0x0040	/* execute */
#define	EXT2_S_IRWXG	0x0038	/* group access rights mask */
#define	EXT2_S_IRGRP	0x0020	/* read */
#define	EXT2_S_IWGRP	0x0010	/* write */
#define	EXT2_S_IXGRP	0x0008	/* execute */
#define	EXT2_S_IRWXO	0x0007	/* others access rights mask */
#define	EXT2_S_IROTH	0x0004	/* read */
#define	EXT2_S_IWOTH	0x0002	/* write */
#define	EXT2_S_IXOTH	0x0001	/* execute */

/* used in directory entries, for the file_type member */
#define EXT2_FT_UNKNOWN  0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR      2
#define EXT2_FT_CHRDEV   3
#define EXT2_FT_BLKDEV   4
#define EXT2_FT_FIFO     5
#define EXT2_FT_SOCK     6
#define EXT2_FT_SYMLINK  7

typedef struct ext2_superblock {
        uint32   s_inodes_count;         /* Inodes count */
        uint32   s_blocks_count;         /* Blocks count */
        uint32   s_r_blocks_count;       /* Reserved blocks count */
        uint32   s_free_blocks_count;    /* Free blocks count */
        uint32   s_free_inodes_count;    /* Free inodes count */
        uint32   s_first_data_block;     /* First Data Block */
        uint32   s_log_block_size;       /* Block size */
        uint32   s_log_cluster_size;     /* Allocation cluster size */
        uint32   s_blocks_per_group;     /* # Blocks per group */
        uint32   s_clusters_per_group;   /* # Fragments per group */
        uint32   s_inodes_per_group;     /* # Inodes per group */
        uint32   s_mtime;                /* Mount time */
        uint32   s_wtime;                /* Write time */
        uint32   s_mnt_count;            /* Mount count */
        sint16   s_max_mnt_count;        /* Maximal mount count */
        uint32   s_magic;                /* Magic signature */
        uint32   s_state;                /* File system state */
        uint32   s_errors;               /* Behaviour when detecting errors */
        uint32   s_minor_rev_level;      /* minor revision level */
        uint32   s_lastcheck;            /* time of last check */
        uint32   s_checkinterval;        /* max. time between checks */
        uint32   s_creator_os;           /* OS */
        uint32   s_rev_level;            /* Revision level */
        uint32   s_def_resuid;           /* Default uid for reserved blocks */
        uint32   s_def_resgid;           /* Default gid for reserved blocks */
        /*
         * These fields are for EXT2_DYNAMIC_REV superblocks only.
         *
         * Note: the difference between the compatible feature set and
         * the incompatible feature set is that if there is a bit set
         * in the incompatible feature set that the kernel doesn't
         * know about, it should refuse to mount the filesystem.
         *
         * e2fsck's requirements are more strict; if it doesn't know
         * about a feature in either the compatible or incompatible
         * feature set, it must abort and not try to meddle with
         * things it doesn't understand...
         */
        uint32   s_first_ino;            /* First non-reserved inode */
        uint32   s_inode_size;           /* size of inode structure */
        uint32   s_block_group_nr;       /* block group # of this superblock */
        uint32   s_feature_compat;       /* compatible feature set */
        uint32   s_feature_incompat;     /* incompatible feature set */
        uint32   s_feature_ro_compat;    /* readonly-compatible feature set */
        uint8    s_uuid[16];             /* 128-bit uuid for volume */
        char    s_volume_name[16];      /* volume name */
        char    s_last_mounted[64];     /* directory where last mounted */
        uint32   s_algorithm_usage_bitmap; /* For compression */
        /*
         * Performance hints.  Directory preallocation should only
         * happen if the EXT2_FEATURE_COMPAT_DIR_PREALLOC flag is on.
         */
        uint8    s_prealloc_blocks;      /* Nr of blocks to try to preallocate*/
        uint8    s_prealloc_dir_blocks;  /* Nr to preallocate for dirs */
        uint32   s_reserved_gdt_blocks;  /* Per group table for online growth */
        /*
         * Journaling support valid if EXT2_FEATURE_COMPAT_HAS_JOURNAL set.
         */
        uint8    s_journal_uuid[16];     /* uuid of journal superblock */
        uint32   s_journal_inum;         /* inode number of journal file */
        uint32   s_journal_dev;          /* device number of journal file */
        uint32   s_last_orphan;          /* start of list of inodes to delete */
        uint32   s_hash_seed[4];         /* HTREE hash seed */
        uint8    s_def_hash_version;     /* Default hash version to use */
        uint8    s_jnl_backup_type;      /* Default type of journal backup */
        uint32   s_desc_size;            /* Group desc. size: INCOMPAT_64BIT */
        uint32   s_default_mount_opts;
        uint32   s_first_meta_bg;        /* First metablock group */
        uint32   s_mkfs_time;            /* When the filesystem was created */
        uint32   s_jnl_blocks[17];       /* Backup of the journal inode */
        uint32   s_blocks_count_hi;      /* Blocks count high 32bits */
        uint32   s_r_blocks_count_hi;    /* Reserved blocks count high 32 bits*/
        uint32   s_free_blocks_hi;       /* Free blocks count */
        uint32   s_min_extra_isize;      /* All inodes have at least # bytes */
        uint32   s_want_extra_isize;     /* New inodes should reserve # bytes */
        uint32   s_flags;                /* Miscellaneous flags */
        uint32   s_raid_stride;          /* RAID stride */
        uint32   s_mmp_update_interval;  /* # seconds to wait in MMP checking */
        uint64   s_mmp_block;            /* Block for multi-mount protection */
        uint32   s_raid_stripe_width;    /* blocks on all data disks (N*stride)*/
        uint8    s_log_groups_per_flex;  /* FLEX_BG group size */
        uint8    s_reserved_char_pad;
        uint32   s_reserved_pad;         /* Padding to next 32bits */
        uint64   s_kbytes_written;       /* nr of lifetime kilobytes written */
        uint32   s_snapshot_inum;        /* Inode number of active snapshot */
        uint32   s_snapshot_id;          /* sequential ID of active snapshot */
        uint64   s_snapshot_r_blocks_count; /* reserved blocks for active
                                              snapshot's future use */
        uint32   s_snapshot_list;        /* inode number of the head of the on-disk snapshot list */
#define EXT4_S_ERR_START ext4_offsetof(struct ext2_super_block, s_error_count)
        uint32   s_error_count;          /* number of fs errors */
        uint32   s_first_error_time;     /* first time an error happened */
        uint32   s_first_error_ino;      /* inode involved in first error */
        uint64   s_first_error_block;    /* block involved of first error */
        uint8    s_first_error_func[32]; /* function where the error happened */
        uint32   s_first_error_line;     /* line number where error happened */
        uint32   s_last_error_time;      /* most recent time of an error */
        uint32   s_last_error_ino;       /* inode involved in last error */
        uint32   s_last_error_line;      /* line number where error happened */
        uint64   s_last_error_block;     /* block involved of last error */
        uint8    s_last_error_func[32];  /* function where the error happened */
#define EXT4_S_ERR_END ext4_offsetof(struct ext2_super_block, s_mount_opts)
        uint8    s_mount_opts[64];
        uint32   s_usr_quota_inum;       /* inode number of user quota file */
        uint32   s_grp_quota_inum;       /* inode number of group quota file */
        uint32   s_overhead_blocks;      /* overhead blocks/clusters in fs */
        uint32   s_backup_bgs[2];        /* If sparse_super2 enabled */
        uint32   s_reserved[106];        /* Padding to the end of the block */
        uint32   s_checksum;             /* crc32c(superblock) */
} ext2_superblock_t;

typedef struct ext2_inode {
        uint16   i_mode;         /* File mode */
        uint16   i_uid;          /* Low 16 bits of Owner Uid */
        uint32   i_size;         /* Size in bytes */
        uint32   i_atime;        /* Access time */
        uint32   i_ctime;        /* Inode change time */
        uint32   i_mtime;        /* Modification time */
        uint32   i_dtime;        /* Deletion Time */
        uint16   i_gid;          /* Low 16 bits of Group Id */
        uint16   i_links_count;  /* Links count */
        uint32   i_blocks;       /* Blocks count */
        uint32   i_flags;        /* File flags */
		uint32   l_i_version; /* was l_i_reserved1 */
        uint32   i_direct[12];   /* 12 direct blocks */
		uint32   i_singly;       /* block pointer to (blocksize/4) blocks */
		uint32   i_doubly;       /* block pointer to (blocksize/4) singly indirect blocks */
        uint32   i_triply;       /* block pointer to (blocksize/4) doubly indirect blocks */
        uint32   i_generation;   /* File version (for NFS) */
        uint32   i_file_acl;     /* File ACL */
        uint32   i_size_high;    /* Formerly i_dir_acl, directory ACL */
        uint32   i_faddr;        /* Fragment address */
		uint16   l_i_blocks_hi;
		uint16   l_i_file_acl_high;
		uint16   l_i_uid_high;   /* these 2 fields    */
		uint16   l_i_gid_high;   /* were reserved2[0] */
		uint16   l_i_checksum_lo; /* crc32c(uuid+inum+inode) */
		uint16   l_i_reserved;
} ext2_inode_t;

typedef struct ext2_bgd {
	uint32 bg_block_bitmap;
	uint32 bg_inode_bitmap;
	uint32 bg_inode_table;
	uint16 bg_free_blocks_count;
	uint16 bg_free_inodes_count;
	uint16 bg_used_dirs_count;
	uint16 bg_pad;
	uint32 bg_reserved[3];
} __attribute__((packed)) ext2_bgd_t;

typedef struct ext2_direntry {
	uint32 inode;
	uint16 rec_len;
	uint8 name_len;
	uint8 file_type;
	char name[255]; // actually only name_len bytes long
} __attribute__((packed)) ext2_direntry_t;

typedef struct ext2_partition {
	struct ext2_superblock super;
	mountpoint_t *mp;
	ata_device_t *dev;
	partition_t *part;
	ext2_bgd_t *bgdt;
	uint32 blocksize;
} ext2_partition_t;

#endif
