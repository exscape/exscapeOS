#ifndef _SYS_DIRENT_H
#define _SYS_DIRENT_H

#include <sys/types.h>

#define DIRENT_NAME_LEN 256

/* POSIX struct dirent */
struct dirent {
	uint32 d_ino;
	uint16 d_dev; // custom field, to allow a dirent to uniquely specify a file
	uint16 __pad;
	uint16 d_reclen;
	uint8 d_type;
	uint8 d_namlen;
	char d_name[DIRENT_NAME_LEN];
};

#define _DIRENT_HAVE_D_NAMLEN
#define _DIRENT_HAVE_D_RECLEN
#define _DIRENT_HAVE_D_TYPE

// struct dirent.flags
enum {
	DT_UNKNOWN = 0,
	DT_FIFO = 1,
	DT_CHR = 2,
	DT_DIR = 4,
	DT_BLK = 6,
	DT_REG = 8,
	DT_LNK = 10,
	DT_SOCK = 12,
    DT_WHT = 14
};

#endif
