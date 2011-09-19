#include <string.h>

/* memset and memcpy are implemented in assembly, and reside in separate source files */

char *strcpy(char *restrict s1, const char *restrict s2) {
	char *ret = s1;

	while (*s2 != 0) {
		*s1++ = *s2++;
	}

	*s1 = 0;

	return ret;
}

int strcmp(const char *s1, const char *s2) {
	for (; *s1 == *s2; ++s1, ++s2) {
		if (*s1 == 0)
			return 0;
	}
	
	return *( (unsigned char *)s1) < *( (unsigned char *)s2) ? -1 : 1;
}

size_t strlen(const char *str) {
	size_t len = 0;

	while (*str++ != 0) {
		len++;
	}

	return len;
}
