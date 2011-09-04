#include <types.h>
#include <stdlib.h> /* itoa(), reverse() */
#include <string.h> /* memset(), strlen() */
#include <kernutil.h> /* inb, inw, outw */
#include <monitor.h> /* printing, scrolling etc. */
#include <gdtidt.h> /* GDT / IDT functions */
#include <stdio.h>
#include <keyboard.h>

//#define DIVZERO_10_SEC /* divides by zero every 10 second, to test exceptions */

// TODO: Proper makefile for .s -> .o and linking

void get_time(Time *);

void get_time(Time *t) {
	/* This function isn't pretty, at all, but it didn't appear very easy to share
	   structs between nasm and C... So I chose inline assembly. */
	unsigned char yeartmp = 0;

	asm(
		/* make sure the update flag isn't set */
		".ll:"
		"movb $10, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"testb $0x80, %%al;"
		"jne .ll;"

		/* fetch the year (00-99) */
		"movb $0x09, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[yeartmp];"

		/* month */
		"movb $0x08, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[month];"

		/* day of month */
		"movb $0x07, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[day];"

		/* hour (12h or 24h) */
		"movb $0x04, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[hour];"

		/* minute */
		"movb $0x02, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[minute];"

		/* second */
		"movb $0x0, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[second];"

		: 
		[yeartmp]"=m"(yeartmp),
		[month]"=m"(t->month),
		[day]  "=m"(t->day),
		[hour] "=m"(t->hour),
		[minute]"=m"(t->minute),
		[second]"=m"(t->second)
		: : "%al", "memory");

	/* Fetch CMOS status register B */
	unsigned char regb;
	asm("movb $0xb, %%al;" // status reg B
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[regb];"
		:
		[regb]"=m"(regb)
		: : "%al", "memory");

	/* These bits describe the output from the RTC - whether the data is
	 * in BCD or binary form, and whether the clock is 12-hour or 24-hour.
	 */
	unsigned char is_bcd, is_24_hour;
	is_bcd     = (regb & 4) ? 0 : 1; /* [sic] */
	is_24_hour = (regb & 2) ? 1 : 0; 

	if (is_bcd) {
#define bcd_to_bin(x) ((x / 16) * 10) + (unsigned char)(x & (unsigned char)0xf)
		// Convert from BCD to decimal form
		yeartmp   = bcd_to_bin(yeartmp);
		t->month  = bcd_to_bin(t->month);
		t->day    = bcd_to_bin(t->day);
		t->hour   = bcd_to_bin(t->hour);
		t->minute = bcd_to_bin(t->minute);
		t->second = bcd_to_bin(t->second);
	}

/* Used to debug exception handling */
#ifdef DIVZERO_10_SEC
	if (t->second % 10 == 0) {
		asm("mov $0xDEADBEEF, %eax;"
			"mov $0, %ebx;"
			"div %ebx;"); 
	}
#endif

	if (!is_24_hour) {
		// TODO: Get PM flag and adjust t->hour
	}

	t->year = 2000 + yeartmp; // TODO: get century from RTC (in case this lives on for 89+ years! ;)
}

void kmain(void* mbd, unsigned int magic) {
	if (magic != 0x2BADB002) {
		panic("Invalid magic received from bootloader!");
	}
	/*
	 * The multiboot header structure, mbd, is defined here:
	 * http://www.gnu.org/software/grub/manual/multiboot/multiboot.html#multiboot_002eh
	 */

	mbd = mbd; // Silence warning

	/* Time to get started initializing things! */
	gdt_install();

	/* Load the IDT */
	idt_install();

	/* Enable interrupts */
	enable_interrupts();

	/* Set up the keyboard callback */
	register_interrupt_handler(IRQ1, keyboard_callback);

	clrscr();

	printk("Hello, world! %s() in action!\n", "printk");

	Time t;
	memset(&t, 0, sizeof(t));
	get_time(&t);

	for (;;) {
		get_time(&t);
		print_time(&t);
	}
}
