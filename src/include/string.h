#ifndef _STRING_H
#define _STRING_H

#include <types.h>

void *memset(void *addr, int c, size_t n);
void *memsetw(void *dst, uint16 val, size_t count);
size_t strlen(const char *str);
void *memcpy(void *restrict s1, const void *restrict s2, size_t n);

char *strcpy(char *restrict s1, const char *restrict s2);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);

char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);

size_t strlcpy(char *dst, const char *src, size_t size);

int stricmp(const char *s1, const char *s2);

/* These three are public domain implementations, i.e. not written by me */
size_t strcspn(const char *s1, const char *s2);
size_t strspn(const char *s1, const char *s2);
char *strtok_r(char *s, const char *delimiters, char **lasts);

/* This doesn't really belong in string.h. */
int isspace(int c);

/* Remove leading and trailing whitespace. Modifies the original string. The returned pointer
 * may be different from the one passed in - if there was leading whitespace. */
/* This is NOT a standard function! */
char *trim(char *str);

#endif
