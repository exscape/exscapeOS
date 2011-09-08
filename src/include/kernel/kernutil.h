#ifndef _KERNUTIL_H
#define _KERNUTIL_H

#include <types.h>

void   outb(uint16 port, uint8 value);
uint8  inb(uint16 port);
uint16 inw(uint16 port);
void panic(const char *str);
void reset(void);

#define assert(b) ((b) ? (void)0 : panic_assert(__FILE__, __LINE__, #b))
extern void panic_assert(const char *file, uint32 line, const char *desc);
	
/* timer.c */
void timer_install(void);

#endif
