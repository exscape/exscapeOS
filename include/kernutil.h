#ifndef _KERNUTIL_H
#define _KERNUTIL_H

#include <types.h>

void   outb(uint16 port, uint8 value);
uint8  inb(uint16 port);
uint16 inw(uint16 port);
void panic(const char *str);
void reset(void);

#endif
