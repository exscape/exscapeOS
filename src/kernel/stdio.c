#include <kernel/stdio.h>
#include <kernel/vfs.h>
#include <kernel/kernutil.h>
#include <kernel/console.h>

/* Implements I/O for the standard streams */

int stdio_read(int fd, void *buf, size_t length) {
	assert(fd >= 0 && fd <= 2);
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
				update_cursor();
			}
		}

		if (c == '\r' || c == '\n') {
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
	const char *p = (const char *)buf;
	assert(fd >= 0 && fd <= 2);

	if (fd == 0) {
		return 0; // We can't write to stdin!
	}

	// fd must now be 1 or 2 - they are treated the same, as there is no
	// buffering on either one.

	size_t ret = 0;

	for (size_t i = 0; i < length && *p; i++) {
		assert(p < (const char *)buf + length);
		putchar(*p++);
		ret++;
	}
	update_cursor();

	return ret;
}

int stdio_close(int fd) {
	panic("TODO: stdio_close()"); // TODO

	return 0;
}
