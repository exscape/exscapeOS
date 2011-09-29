#ifndef _KEYBOARD_H
#define _KEYBOARD_H

void keyboard_callback(registers_t regs);
void init_keyboard(void);
unsigned char getchar(void);

#endif
