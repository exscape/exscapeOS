#ifndef _FILEIO_H
#define _FILEIO_H

#include <types.h>
#include <kernel/vfs.h>

#define MAX_OPEN_FILES 128

#define O_RDONLY 0

#define MAX_DEVS 8
extern void *devtable[MAX_DEVS];
extern uint32 next_dev;

typedef struct open_file {
	dev_t dev;
	ino_t ino;
	ino_t _cur_ino; // current cluster number, i.e. when offset != 0
	off_t offset;
	off_t size;
	mountpoint_t *mp;
	char *path;
} open_file_t;

int open(const char *path, int mode);
int read(int fd, void *buf, int length);
int close(int fd);
DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);

#endif
