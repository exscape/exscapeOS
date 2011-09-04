#include <types.h>
#include <gdtidt.h> /* isr_t, registers_t */
#include <kernutil.h> /* panic */
#include <monitor.h>

void register_interrupt_handler(uint8 n, isr_t handler);

void keyboard_callback(registers_t regs) {
	regs = regs; // avoid a warning
	unsigned char scancode = inb(0x60);
	printk("%x ", scancode);
}
