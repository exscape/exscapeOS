#include <kernutil.h>
#include <stdio.h>
#include <monitor.h>
#include <gdtidt.h>

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
	printk("\nPANIC: %s", str);
	asm("hangloop: hlt ; jmp hangloop");
	// TODO: Does the halt work properly?
}

void reset(void) {
	// Resets the CPU by causing a triple fault.
	struct idt_ptr p = {0};
	asm("lgdt (%0);"
		"int $3;"
		: : "r"(&p));
}
