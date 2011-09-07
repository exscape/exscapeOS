#ifndef _STRING_H
#define _STRING_H

#include <types.h>

void *memset(void *addr, int c, size_t n);
size_t strlen(const char *str);
void *memcpy(void *restrict s1, const void *restrict s2, size_t n);

#endif
