#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <kernel/interrupts.h> /* registers_t */

void keyboard_callback(registers_t regs);
void init_keyboard(void);
unsigned char getchar(void);
uint32 availkeys(void);

#endif
