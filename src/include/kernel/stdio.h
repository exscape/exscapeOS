#ifndef _KERNEL_STDIO_H
#define _KERNEL_STDIO_H

#include <kernel/vfs.h>
#include <kernel/kernutil.h>

/* Implements I/O for the standard streams */

int stdio_read(int fd, void *buf, size_t length);
int stdio_write(int fd, const void *buf, size_t length);
int stdio_close(int fd, struct open_file *file);
int stdio_fstat(int fd, struct stat *st);

#endif
