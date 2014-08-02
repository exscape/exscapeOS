#ifndef _SYS_DIRENT_H
#define _SYS_DIRENT_H

#include <sys/types.h>

#define MAXNAMLEN 256

/* POSIX struct dirent */
struct dirent {
	uint32 d_ino;
	uint16 d_dev; // custom field, to allow a dirent to uniquely specify a file
	uint16 __pad;
	uint16 d_reclen;
	uint8 d_type;
	uint8 d_namlen;
	char d_name[MAXNAMLEN];
};

#define _DIRENT_HAVE_D_NAMLEN
#define _DIRENT_HAVE_D_RECLEN
#define _DIRENT_HAVE_D_TYPE

// struct dirent.d_type flags
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

#ifndef  _EXSCAPEOS_KERNEL
typedef struct {
    int dd_fd;		/* directory file */
    int dd_loc;		/* position in buffer */
    int dd_seek;
    char *dd_buf;	/* buffer */
    int dd_len;		/* buffer length */
    int dd_size;	/* amount of data in buffer */
//	_LOCK_RECURSIVE_T dd_lock;
} DIR;

struct dirent *readdir(DIR *dirp);
DIR *opendir(const char *name);
int closedir(DIR *dirp);

int scandir(const char *dirname, struct dirent ***namelist, int (*select)(const struct dirent *), int (*dcomp)(const struct dirent **, const struct dirent **));
void rewinddir(DIR *dirp);
void seekdir(DIR *dirp, long loc);
long telldir(DIR *dirp);
#endif

#endif
