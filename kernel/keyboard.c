#include <types.h>
#include <gdtidt.h> /* isr_t, registers_t */
#include <kernutil.h> /* panic */

void register_interrupt_handler(uint8 n, isr_t handler);

void keyboard_callback(registers_t regs) {
	panic("Keyboard!");
	regs = regs;
}
