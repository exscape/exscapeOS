#include <kernel/stdio.h>
#include <kernel/vfs.h>
#include <kernel/kernutil.h>
#include <kernel/console.h>
#include <string.h>
#include <sys/errno.h>

/* Implements I/O for the standard streams */

int stdio_read(int fd, void *buf, size_t length) {
	struct open_file *filp = get_filp(fd);
	if (!filp)
		return -EBADF;
	if (filp->ino != 0) {
		assert(filp->ino <= 2);
		// Read on stdout or stderr
		return 0;
	}

	char *p = (char *)buf;

	if (fd != 0) {
		return 0; // Don't allow reading from stdout/stderr
	}

	int ret = 0;
	while (p < (char *)buf + length - 1 /* NULL termination */) {
		char c = getchar();

		if (c >= ' ' || c == '\n') {
			putchar(c); // echo to screen
			update_cursor();
		}
		else if (c == '\b') {
			if (p > (char *)buf) {
				p--;
				putchar(c);
				putchar(' ');
				putchar(c);
				update_cursor();
				ret--;
			}
		}
		else if (c == -1) {
			// EOF sent by Ctrl-D
			if (ret > 0)
				continue;
			else {
				putchar('^');
				putchar('D');
				return 0;
			}
		}

		if (c == '\r' || c == '\n') {
			ret++;
			*p++ = c;
			*p = 0;
			return ret;
		}
		else if (c != '\b') {
			*p++ = c;
			ret++;
		}
	}

	assert(p < (char *)buf + length);
	*p = 0;

	return ret;
}

int stdio_write(int fd, const void *buf, size_t length) {
	struct open_file *filp = get_filp(fd);
	if (!filp)
		return -EBADF;
	if (filp->ino == 0) {
		// Write on stdin
		return 0;
	}

	// stdout and stderr are treated identically; neither is buffered

	const char *p = (const char *)buf;
	size_t ret = 0;

	for (size_t i = 0; i < length && *p; i++) {
		assert(p < (const char *)buf + length);
		putchar(*p++);
		ret++;
	}
	update_cursor();

	return ret;
}

int stdio_close(int fd, struct open_file *file) {
	// We really don't need to do anything at all,
	// except for the stuff that close() does for us after we return!

	return 0;
}

int stdio_fstat(int fd, struct stat *st) {
	struct open_file *filp = get_filp(fd);
	if (!filp)
		return -EBADF;
	memset(st, 0, sizeof(struct stat));
	st->st_dev = 0xffff; // TODO
	st->st_ino = filp->ino;
	st->st_mode = 0666 | S_IFCHR;
	st->st_blksize = 128;

	return 0;
}
