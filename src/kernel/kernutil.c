#include <kernel/kernutil.h>
#include <stdio.h>
#include <kernel/console.h>
#include <kernel/interrupts.h>
#include <string.h>
#include <kernel/task.h>

extern task_t *current_task;

// Write a byte to the specified port
void outb(uint16 port, uint8 value)
{
    asm volatile ("outb %1, %0" : : "dN" (port), "a" (value));
}

void outw(uint16 port, uint16 value)
{
    asm volatile ("outw %1, %0" : : "dN" (port), "a" (value));
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
	console_switch(&kernel_console);
	asm volatile("cli");
	printk("\nPANIC: %s\nCurrent task: %u (%s)", str, current_task->id, current_task->name);
	asm volatile("0: hlt ; jmp 0b");
}

extern void panic_assert(const char *file, uint32 line, const char *desc) {
	/* Call panic() instead of doing this ourselves, so that breakpoints
	 * on panic() catches assertions as well */
	char buf[1024];
	sprintf(buf, "Assertion failed: %s (%s:%d)\n", desc, file, line);
	panic(buf);
}

void reset(void) {
	/* Resets the CPU by causing a triple fault.
	 * More specifically, it creates a NULL IDT pointer, loads the "IDT", and causes an interrupt.
	 * There is, of course, no handler available to handle that interrupt, which eventually causes a triple fault. */
	struct idt_ptr p;
	memset(&p, 0, sizeof(struct idt_ptr));
	asm volatile("lgdt (%0);"
		"int $3;"
		: : "r"(&p));
}

void reboot(void) {
	/* Restarts the computer. Initially, this simply calls reset(); in the future,
	 * it will call the necessary cleanup functions, flush disk caches etc. */
	reset();
}
