#include <sys/syslimits.h>
#include <kernel/pipe.h>
#include <kernel/kernutil.h>
#include <kernel/vmm.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <string.h>
#include <kernel/heap.h>
#include <kernel/task.h>
#include <kernel/time.h>

int pipe_read(int fd, void *buf, size_t count);
int pipe_write(int fd, const void *buf, size_t count);
int pipe_close(int fd, struct open_file *file);
int pipe_fstat(int fd, struct stat *st);

int pipe(int fildes[2]) {
	int rd, wr;
	struct open_file *read_end = new_filp(&rd);
	if (read_end == NULL || rd < 0)
		return -ENFILE;

	struct open_file *write_end = new_filp(&wr);
	if (write_end == NULL || wr < 0) {
		destroy_filp(rd);
		return -ENFILE;
	}

	read_end->count++;
	write_end->count++;

	// Set up the pipe
	struct pipe *p = kmalloc(sizeof(struct pipe));
	memset(p, 0, sizeof(struct pipe));

	assert(PIPE_BUFFER_SIZE > PIPE_BUF);

	p->buffer = kmalloc(PIPE_BUFFER_SIZE);
	p->reader = read_end;
	p->writer = write_end;
	p->read_pos = p->buffer;
	p->write_pos = p->buffer;
	p->max_pos = p->buffer + PIPE_BUFFER_SIZE;
	p->lock = mutex_create();
	/* leave mtime and atime as 0 */

	// Link the pipe to the ends
	read_end->data = p;
	write_end->data = p;
	read_end->dev = DEV_PIPE;
	write_end->dev = DEV_PIPE;

	// Set up the fops
	read_end->fops.read = pipe_read;
	read_end->fops.write = pipe_write;
	read_end->fops.close = pipe_close;
	read_end->fops.fstat = pipe_fstat;

	write_end->fops.read = pipe_read;
	write_end->fops.write = pipe_write;
	write_end->fops.close = pipe_close;
	write_end->fops.fstat = pipe_fstat;

	// ... and, finally:
	fildes[0] = rd;
	fildes[1] = wr;

	return 0;
}

int sys_pipe(int fildes[2]) {
	if (fildes == NULL || !CHECK_ACCESS_WRITE(fildes, 2 * sizeof(int)))
		return -EFAULT;

	return pipe(fildes);
}

#define min(a,b) ( (a < b) ? a : b )

int pipe_read(int fd, void *buf, size_t count) { 
	struct open_file *file = get_filp(fd);
	if (file == NULL)
		return -EBADF;

	assert(file->dev == DEV_PIPE);
	assert(file->data != NULL);

	struct pipe *p = (struct pipe *)file->data;

	if (p->writer == NULL && p->bytes_avail == 0) {
		// No write end, and no data!
		return 0;
	}

	if (file == p->writer) {
		// read() on write end of pipe
		return -EBADF;
	}

	// OK, so we either have data waiting already, or a write end that can provide some.
	while ((volatile uint32)p->bytes_avail == 0) {
		// TODO: block properly!
		sleep(30);
	}

	// We have some data!
	mutex_lock(p->lock);

	assert(p->bytes_avail > 0);

	// # bytes that can be read without wrapping
	// (there may be fewer bytes available!)
	uint32 n = p->max_pos - p->read_pos;

	uint32 bytes_read = 0;

	// First, read the stuff we can without wrapping
	uint32 to_read = min(min(count, n), p->bytes_avail);
	assert(p->bytes_avail >= to_read);
	assert(p->read_pos + to_read <= p->max_pos);
	memcpy((char *)buf, p->read_pos, to_read);
	p->read_pos += to_read;
	p->bytes_avail -= to_read;
	bytes_read += to_read;

	p->atime = kern_time();

	// Wrap, if necessary
	if (p->read_pos >= p->max_pos) {
		assert(p->read_pos == p->max_pos); // being *greater* than max_pos would be a bug!
		p->read_pos = p->buffer;
	}

	if (count > bytes_read && p->bytes_avail > 0) {
		// But wait, there's more!
		to_read = min(p->bytes_avail, count);
		assert(p->bytes_avail >= to_read);
		assert(p->read_pos + to_read <= p->max_pos);
		memcpy((char *)buf + bytes_read, p->read_pos, to_read);
		p->read_pos += to_read;
		p->bytes_avail -= to_read;
		bytes_read += to_read;
	}

	// There might be even more data, but if there is, the caller will have to
	// call again. We can't block until we've read the exact count, as that would
	// break spec!

#if 0
	char *tmp = kmalloc(bytes_read+1);
	memset(tmp, 0, bytes_read+1);
	memcpy(tmp, buf, bytes_read);
	for (uint32 i=0; i < bytes_read; i++) {
		if (tmp[i] == '\n') tmp[i] = '_';
	}
	printk("pipe_read read %u bytes  : \"%s\"\n", bytes_read, tmp);
#endif

	mutex_unlock(p->lock);
	return bytes_read;
}

int pipe_write(int fd, const void *buf, size_t count) {
	struct open_file *file = get_filp(fd);
	if (file == NULL)
		return -EBADF;

	assert(file->dev == DEV_PIPE);
	assert(file->data != NULL);

	struct pipe *p = (struct pipe *)file->data;

	if (file == p->reader) {
		// write() on read end of pipe
		return -EBADF;
	}

	uint32 bytes_written = 0;

	while (count > bytes_written) {
		if (p->reader == NULL || p->reader->count <= 0) {
			if (p->reader)
				assert(p->reader->count == 0);
			// Write to pipe with no reader!
			// TODO: send SIGPIPE as well
			return -EPIPE; // TODO: should we return bytes_written here if > 0?
		}

		// First, block until we can write anything at all (all writes below PIPE_BUF bytes must be atomic)
		while (PIPE_BUFFER_SIZE - (volatile uint32)p->bytes_avail < min(PIPE_BUF, count)) {
			// TODO: block properly!
			sleep(30);
		}

		if (p->reader == NULL || p->reader->count <= 0) {
			if (p->reader)
				assert(p->reader->count == 0);
			// Write to pipe with no reader!
			// TODO: send SIGPIPE as well
			return -EPIPE; // TODO: should we return bytes_written here if > 0?
		}

		// OK, we should be able to fit min(PIPE_BUF, count) bytes now.

		mutex_lock(p->lock);

		// # bytes that can be written without wrapping
		uint32 n = p->max_pos - p->write_pos;

		// First, write the stuff we can without wrapping
		uint32 to_write = min(min(count, n), PIPE_BUFFER_SIZE - p->bytes_avail);
		assert(PIPE_BUFFER_SIZE - p->bytes_avail >= to_write);
		assert(p->write_pos + to_write <= p->max_pos);

		memcpy(p->write_pos, buf, to_write);
		p->write_pos += to_write;
		p->bytes_avail += to_write;
		bytes_written += to_write;

		// Wrap, if necessary
		if (p->write_pos >= p->max_pos) {
			assert(p->write_pos == p->max_pos); // being *greater* than max_pos would be a bug!
			p->write_pos = p->buffer;
		}

		// TODO: assertions relating read_pos and write_pos

		if (count > bytes_written && PIPE_BUFFER_SIZE - p->bytes_avail > 0) {
			// But wait, there's more!
			to_write = min(PIPE_BUFFER_SIZE - p->bytes_avail, count);
			assert(PIPE_BUFFER_SIZE - p->bytes_avail >= to_write);
			assert(p->write_pos + to_write <= p->max_pos);
			memcpy(p->write_pos, (char *)buf + bytes_written, to_write);
			p->write_pos += to_write;
			p->bytes_avail += to_write;
			bytes_written += to_write;
		}

		p->atime = kern_time();
		p->mtime = p->atime;

		mutex_unlock(p->lock);
	}

	return bytes_written;
}

int pipe_close(int fd, struct open_file *file) {
	if (!file)
		return -EBADF;

	assert(file->data != NULL);

	struct pipe *p = (struct pipe *)file->data;
	assert(p->reader == file || p->writer == file);

	if (p->reader == file) {
		if (file->count == 1) {
			// We are called prior to count-- in close, so this pipe end WILL close fully
			p->reader = NULL;
			// TODO: abort blocking writers
		}
	}
	else {
		if (file->count == 1) {
			p->writer = NULL;
			// TODO: abort blocking readers
		}
	}

	if (p->reader == NULL && p->writer == NULL) {
		//panic("freeing pipe stuff");
		// Free everything
		mutex_destroy(p->lock);
		memset(p->buffer, 0, PIPE_BUFFER_SIZE); // TODO: remove this eventually
		kfree(p->buffer);
		memset(p, 0, sizeof(struct pipe));
		kfree(p);
	}

	return 0;
}

int pipe_fstat(int fd, struct stat *st) {
	struct open_file *file = get_filp(fd);
	if (!file)
		return -EBADF;
	if (file->data == NULL)
		return -EBADF;

	struct pipe *p = (struct pipe *)file->data;

	memset(st, 0, sizeof(struct stat));
	st->st_dev = DEV_PIPE;
	st->st_ino = 0;
	st->st_nlink = 0;
	st->st_size = p->bytes_avail; // TODO: what do other OSes do?
	st->st_blksize = PIPE_BUFFER_SIZE;
	st->st_mode = _IFIFO | 0600;
	st->st_mtime = p->mtime;
	st->st_ctime = p->mtime;
	st->st_atime = p->atime;

	return 0;
}
