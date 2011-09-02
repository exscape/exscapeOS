#ifndef _MONITOR_H
#define _MONITOR_H
#include <types.h>

int putchar(int c);
void print(const char *str);
void clrscr(void);
void update_cursor(void);
void scroll(void);
void print_time(const Time *t);

#endif
