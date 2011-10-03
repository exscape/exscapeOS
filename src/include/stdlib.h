#ifndef _STDLIB_H
#define _STDLIB_H

#include <types.h>

void itoa(signed long, char *);
void reverse(char *s);

int rand(void);
void srand(uint32 seed);
int isdigit(int c);
int ipow(int base, int exp); /* used for atoi */
int atoi(const char *str);

#endif
