#include <string.h>

void *memset(void *addr, int c, size_t n) {
	unsigned char *p = addr;

	for (size_t i = 0; i < n; i++) {
		*p++ = (unsigned char)c;
	}

	return addr;
}

size_t strlen(const char *str) {
	size_t len = 0;

	while (*str++ != 0) {
		len++;
	}

	return len;
}

