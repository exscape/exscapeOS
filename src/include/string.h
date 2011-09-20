#ifndef _STRING_H
#define _STRING_H

#include <types.h>

void *memset(void *addr, int c, size_t n);
void *memsetw(void *dst, uint16 val, size_t count);
size_t strlen(const char *str);
void *memcpy(void *restrict s1, const void *restrict s2, size_t n);

char *strcpy(char *restrict s1, const char *restrict s2);
int strcmp(const char *s1, const char *s2);

size_t strlcpy(char *dst, const char *src, size_t size);

#endif
