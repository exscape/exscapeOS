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

/*
 *** memcpy now resides in memcpy.s

void *memcpy(void *restrict s1, const void *restrict s2, size_t n) {
	// Since we can't dereference a void pointer...
	char *p1 = s1;
	const char *p2 = s2;

	while (n-- > 0) {
		*p1++ = *p2++;
	}

	// Return value should be the original s1 pointer
	return s1;
}
*/

