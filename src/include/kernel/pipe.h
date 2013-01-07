#ifndef _PIPE_H
#define _PIPE_H

#include <kernel/vfs.h>
#include <kernel/mutex.h>

#define PIPE_BUFFER_SIZE 65536

int pipe(int fildes[2]);
int sys_pipe(int fildes[2]);

struct pipe {
	char *buffer; // PIPE_BUFFER_SIZE bytes
	struct open_file *reader; // may have >1 count
	struct open_file *writer; // may have >1 count
	char *read_pos;
	char *write_pos;
	char *max_pos; // buffer + PIPE_BUFFER_SIZE TODO: OBOE
	uint32 bytes_avail;
	mutex_t *lock;

	time_t mtime;
	time_t atime;
};

#endif
