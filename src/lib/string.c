#include <string.h>
#include <kernel/heap.h>

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

#define tolower(c) (( (c >= 'A' && c <= 'Z') ? (c | 0x20) : c ))
int stricmp(const char *s1, const char *s2) {
	for (; tolower(*s1) == tolower(*s2); ++s1, ++s2) {
		if (*s1 == 0)
			return 0;
	}

	return *( (unsigned char *)s1) < *( (unsigned char *)s2) ? -1 : 1;
}


char *strchr(const char *s, int c_) {
	char c = (char)c_;
	while (*s != 0 && *s != c)
		s++;
	if (*s == c)
		return (char *)s;
	else
		return NULL;
}

char *strdup(const char *s) {
	if (s == NULL)
		return NULL;

	// alloc even if strlen == 0
	size_t len = strlen(s);
	char *ret = kmalloc(len + 1);
	strlcpy(ret, s, len + 1);

	return ret;
}

char *strrchr(const char *s, int c_) {
	char c = (char)c_;
	const char *p = s + strlen(s) - 1;
	if (p < s)
		return NULL;

	while (p > s && *p != c)
		p--;

	if (*p == c)
		return (char *)p;
	else
		return NULL;
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

int strnicmp(const char *s1, const char *s2, size_t n) {
	unsigned char uc1, uc2;
	if (n == 0 || s1 == NULL || s2 == NULL)
		return 0;
	/* Loop, comparing bytes.  */
	while (n-- > 0 && tolower(*s1) == tolower(*s2)) {
		if (n == 0 || *s1 == '\0')
			return 0;
		s1++, s2++;
	}

	uc1 = (*(unsigned char *) s1);
	uc2 = (*(unsigned char *) s2);

	return ((uc1 < uc2) ? -1 : (uc1 > uc2));
}


int memcmp(const void *lhs, const void *rhs, size_t count) {
	const uint8 *us1 = (uint8 *)lhs;
	const uint8 *us2 = (uint8 *)rhs;
	while (count-- != 0) {
		if (*us1 != *us2) 
			return (*us1 < *us2) ? -1 : 1;
		us1++, us2++;
	}

	return 0;
}

size_t strlen(const char *str) {
	size_t len = 0;

	while (*str++ != 0) {
		len++;
	}

	return len;
}

char *strstr(const char *haystack, const char *needle) {
	const char *p1 = haystack, *p2 = needle;
	const char *match = NULL;

	// Empty needle should return the first argument
	if (*p2 == 0)
		return (char *)haystack;

	while (1) {
		while (*p1 != 0 && *p2 != 0 && *p1 != *p2) {
			// Skip along while nothing matches
			p1++;
		}
		if (*p1 == 0 || *p2 == 0) {
			// We reached the end with no match
			break;
		}
		else if (*p1 == *p2) {
			// We found a potential match (first character matches), test the rest
			match = p1;
			while (*p2 != 0 && *p1 == *p2) { p1++, p2++; }
			if (*p2 == 0) {
				// They match all until the end of needle: match!
				return (char *)match;
			}
			else {
				// Mismatch; try again at the start of needle, at the next p1
				match = NULL;
				p2 = needle;
				continue;
			}
		}
	}

	return NULL;
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
	char *d = dst;
	for (size_t i = 0; i < size - 1; i++) {
		*d++ = *src++;
	}

	/* This is strLcpy - make sure to NULL terminate! */
	dst[size - 1] = 0;

	return len;
}

/* By Russ Allbery, public domain code.
 * Mostly because I didn't feel like stopping what I was doing
 * to write a required string function.
 */
size_t strlcat(char *dst, const char *src, size_t size)
{
    size_t used, length, copy;

    used = strlen(dst);
    length = strlen(src);
    if (size > 0 && used < size - 1) {
        copy = (length >= size - used) ? size - used - 1 : length;
        memcpy(dst + used, src, copy);
        dst[used + copy] = '\0';
    }
    return used + length;
}

size_t strcspn(const char *s1, const char *s2)
{
	const char *sc1;
	for (sc1 = s1; *sc1 != '\0'; sc1++)
		if (strchr(s2, *sc1) != NULL)
			return (sc1 - s1);
	return sc1 - s1;            /* terminating nulls match */
}

size_t strspn(const char *s1, const char *s2)
{
	const char *sc1;
	for (sc1 = s1; *sc1 != '\0'; sc1++)
		if (strchr(s2, *sc1) == NULL)
		return (sc1 - s1);
	return sc1 - s1;            /* terminating nulls don't match */
}

char *strtok_r(char *s, const char *delimiters, char **lasts)
{
	char *sbegin, *send;
	sbegin = s ? s : *lasts;
	sbegin += strspn(sbegin, delimiters);

	if (*sbegin == '\0') {
		*lasts = (char *)"";
		return NULL;
	}

	send = sbegin + strcspn(sbegin, delimiters);

	if (*send != '\0')
		*send++ = '\0';

	*lasts = send;

	return sbegin;
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
