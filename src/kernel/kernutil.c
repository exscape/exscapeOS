#include <kernel/kernutil.h>
#include <stdio.h>
#include <kernel/console.h>
#include <kernel/interrupts.h>
#include <string.h>
#include <kernel/task.h>
#include <kernel/backtrace.h>

// Write a byte to the specified port
void outb(uint16 port, uint8 value)
{
    asm volatile ("outb %1, %0" : : "dN" (port), "a" (value));
}

void outw(uint16 port, uint16 value)
{
    asm volatile ("outw %1, %0" : : "dN" (port), "a" (value));
}

void outl(uint16 port, uint32 value)
{
    asm volatile ("outl %1, %0" : : "dN" (port), "a" (value));
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

uint32 inl(uint16 port)
{
   uint32 ret;
   asm volatile ("inl %1, %0" : "=a" (ret) : "dN" (port));
   return ret;
}

extern bool kernel_paniced;
extern char _printk_buf[1024];

void panic(const char *fmt, ...) {
	asm volatile("cli");
	console_switch(&kernel_console);
	console_task = &kernel_task; // always has a console; this way, putchar() won't panic and cause a triple fault
	scrollback_reset();

	va_list args;
	int i;

	printk("\nPANIC: ");

	va_start(args, fmt);
	i = vsprintf(_printk_buf, fmt, args);
	va_end(args);

	if (i > 0) {
		size_t len = strlen(_printk_buf);
		for (size_t j = 0; j < len; j++) {
			putchar(_printk_buf[j]);
		}
	}

	printk("\nCurrent task: %u (%s)", current_task->id, current_task->name);

	kernel_paniced = true;
	update_statusbar();
	//asm volatile("cli; 0: hlt ; jmp 0b");
	for(;;) { sleep(10000000); } // to allow scrollback
}

extern void panic_assert(const char *file, uint32 line, const char *desc) {
	/* Call panic() instead of doing this ourselves, so that breakpoints
	 * on panic() catches assertions as well */
	printk("\nAssertion failure! Backtrace:\n");
	print_backtrace();
	panic("Assertion failed: %s (%s:%d)\n", desc, file, line);
}

void reset(void) {
	/* Resets the CPU by causing a triple fault.
	 * More specifically, it creates a NULL IDT pointer, loads the "IDT", and causes an interrupt.
	 * There is, of course, no handler available to handle that interrupt, which eventually causes a triple fault. */
	struct idt_ptr p;
	memset(&p, 0, sizeof(struct idt_ptr));
	asm volatile("lidt (%0);"
		"int $3;"
		: : "r"(&p));
}

void reboot(void) {
	/* Restarts the computer. Initially, this simply calls reset(); in the future,
	 * it will call the necessary cleanup functions, flush disk caches etc. */
	reset();
}
