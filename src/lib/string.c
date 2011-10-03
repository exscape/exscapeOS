#include <string.h>

/* memset and memcpy are implemented in assembly, and reside in separate source files */

/* NOTE: this doesn't really belong in string.h... */
int isspace(int c) {
	if (c == ' ' || c == '\n' || c == '\t' || c == '\v' || c == '\f' || c == '\r')
		return 1;
	else
		return 0;
}

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

int strncmp(const char *s1, const char *s2, size_t n) {
	unsigned char uc1, uc2;
	if (n == 0 || s1 == NULL || s2 == NULL)
		return 0;
	/* Loop, comparing bytes.  */
	while (n-- > 0 && *s1 == *s2) {
		if (n == 0 || *s1 == '\0')
			return 0;
		s1++, s2++;
	}

	uc1 = (*(unsigned char *) s1);
	uc2 = (*(unsigned char *) s2);

	return ((uc1 < uc2) ? -1 : (uc1 > uc2));
}

size_t strlen(const char *str) {
	size_t len = 0;

	while (*str++ != 0) {
		len++;
	}

	return len;
}

/* Copies the string from src to dst, never writing more than /size/ bytes. Always NULL terminates (if there is at least 1 byte to write to). */
size_t strlcpy(char *dst, const char *src, size_t size) {
	const size_t len = strlen(src);

	if (size >= len + 1) {
		/* Everything fits, just use strcpy() instead */
		strcpy(dst, src);
		return len;
	}
	else if (size == 0)
		return len;

	/* Still here, so it didn't all fit. Copy what fits (leaving 1 byte for NULL termination) */
	for (size_t i = 0; i < size - 1; i++) {
		*dst++ = *src++;
	}

	/* This is strLcpy - make sure to NULL terminate! */
	dst[size - 1] = 0;

	return len;
}

/* Remove leading and trailing whitespace. Modifies the original string. The returned pointer
 * may be different from the one passed in - if there was leading whitespace. */
char *trim(char *str) {
	size_t len = strlen(str);

	if (len == 0)
		return str;

	/* Trim leading whitespace. Needless to say, this will modify the pointer, making it unusable for free() */
	while (isspace(*str)) str++;

	if (*str == 0)
		return str;

	/* Trim trailing whitespace */
	char *end = str + strlen(str) - 1;
	while (end > str && isspace(*end)) end--;
	*(end+1) = 0;

	return str;
}
