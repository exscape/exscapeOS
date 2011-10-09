#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <kernel/interrupts.h> /* registers_t */

#define KEYBUFFER_SIZE 256

void keyboard_callback(uint32 esp);
void init_keyboard(void);

#endif
