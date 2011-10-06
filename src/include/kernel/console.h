#ifndef _CONSOLE_H
#define _CONSOLE_H
#include <types.h>

int putchar(int c);
void init_video(void);
void clrscr(void);
void update_cursor(void);
void scroll(void);
void print_time(const Time *t);
size_t printk(const char *fmt, ...);
int print(const char *s);

extern Point cursor;

#endif
