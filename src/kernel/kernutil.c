#include <kernel/kernutil.h>
#include <stdio.h>
#include <kernel/monitor.h>
#include <kernel/interrupts.h>
#include <string.h>

// Write a byte to the specified port
void outb(uint16 port, uint8 value)
{
    asm volatile ("outb %1, %0" : : "dN" (port), "a" (value));
}

uint8 inb(uint16 port)
{
   uint8 ret;
   asm volatile("inb %1, %0" : "=a" (ret) : "dN" (port));
   return ret;
}

uint16 inw(uint16 port)
{
   uint16 ret;
   asm volatile ("inw %1, %0" : "=a" (ret) : "dN" (port));
   return ret;
} 

void panic(const char *str) {
//	clrscr();
	asm volatile("cli");
	printk("\nPANIC: %s", str);
	asm("hangloop: hlt ; jmp hangloop");
}

extern void panic_assert(const char *file, uint32 line, const char *desc) {
	asm volatile("cli");

	printk("PANIC: Assertion failed: %s (%s:%d)\n", desc, file, line);
	asm("asserthang: hlt; jmp asserthang");
}

void reset(void) {
	/* Resets the CPU by causing a triple fault.
	 * More specifically, it creates a NULL IDT pointer, loads the "IDT", and causes an interrupt.
	 * There is, of course, no handler available to handle that interrupt, which eventually causes a triple fault. */
	struct idt_ptr p;
	memset(&p, 0, sizeof(struct idt_ptr));
	asm("lgdt (%0);"
		"int $3;"
		: : "r"(&p));
}
