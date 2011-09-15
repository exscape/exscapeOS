#include <types.h>
#include <stdlib.h> /* itoa(), reverse() */
#include <string.h> /* memset(), strlen() */
#include <kernel/kernutil.h> /* inb, inw, outw */
#include <kernel/monitor.h> /* printing, scrolling etc. */
#include <kernel/gdtidt.h> /* GDT / IDT functions */
#include <stdio.h>
#include <kernel/keyboard.h>
#include <kernel/timer.h>
#include <kernel/kheap.h>
#include <kernel/paging.h>

//#define DIVZERO_10_SEC /* divides by zero every 10 second, to test exceptions */

// TODO: Move all RTC code to somewhere else - timer.c or a new rtc.c?

void get_time(Time *);

void get_time(Time *t) {
	/* This function isn't pretty, at all, but it didn't appear very easy to share
	   structs between nasm and C... So I chose inline assembly. */
	unsigned char yeartmp = 0;


	/*
	 * This is a big chunk of code used for debugging the RTC routines.
	 * It changes status register B of the CMOS to change the use
	 * of 12h/24h time and BCD/binary representations.
	 * It's still here just in case; rewriting it would be boring.
	 */

/*	
	// Set status reg B for debugging purposes
	unsigned char rb = 0;

	asm(
	// wait until no update is in progress
		".lltmp:"
		"movb $10, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"testb $0x80, %%al;"
		"jne .lltmp;"

	// fetch status reg B
		"movb $0xb, %%al;" // status reg B
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[rb];"
		: [rb]"=m"(rb) : : "%al", "memory");

	// Force 12h time
	rb &= ~2;

	// set status reg B
	asm("cli;"
		"movb $0xb, %%al;"
		"outb %%al, $0x70;"
		"movb %[rb], %%al;"
		"nop;nop;nop;nop;nop;nop;nop;nop;"
		"outb %%al, $0x71;"
		"sti;"
		: : [rb]"m"(rb) : "%al", "memory");

	// Set the clock (to debug AM/PM issues)
	asm("cli;"
		"movb $0x4, %%al;" // set the hour...
		"outb %%al, $0x70;"
		"movb $0, %%al;"  // ...to the time specified
		"nop;nop;nop;nop;nop;nop;nop;nop;"
		"outb %%al, $0x71;"
		"sti;"
		: : : "%al", "memory");
*/
	// END DEBUGGING CODE

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
	asm("movb $0xb, %%al;" // 0xb = status reg B
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

	uint8 time_is_pm = 0;

	/*
	 * If this is a 12-hour clock, mask out the 0x80 bit if it's set.
	 * If it is, the time is PM. If not, it's AM.
	 * This is used below, after BCD decoding.
	 */
	if (!is_24_hour) {
		if ((t->hour & 0x80) != 0) {
			// Remove the bit (to make the hour readable)
			t->hour &= ~0x80;
			// Save this knowledge, though; it's used below
			time_is_pm = 1;
		}
	}

	/* Convert the data from BCD to binary form, if needed */
	if (is_bcd) {
#define bcd_to_bin(x) ((x / 16) * 10) + (unsigned char)(x & (unsigned char)0xf)
		yeartmp   = bcd_to_bin(yeartmp);
		t->month  = bcd_to_bin(t->month);
		t->day    = bcd_to_bin(t->day);
		t->hour   = bcd_to_bin(t->hour);
		t->minute = bcd_to_bin(t->minute);
		t->second = bcd_to_bin(t->second);
	}

	if (!is_24_hour) {
		/* Midnight is stored as 12PM; we want to display it as 00 */
		if (time_is_pm && t->hour == 12)
			t->hour = 0;
		else if (time_is_pm) {
			/* Otherwise, just move it to 24-hour times, as usual */
			t->hour += 12;
		}
	}

/* Used to debug exception handling */
#ifdef DIVZERO_10_SEC
	if (t->second % 10 == 0) {
		asm("mov $0xDEADBEEF, %eax;"
			"mov $0, %ebx;"
			"div %ebx;"); 
	}
#endif

	/*
	 * This will fail for year 2100+, and I frankly don't care enough to fix it.
	 * In fact, I would if I was *sure* it was always stored it register 0x32.
	 */
	t->year = 2000 + yeartmp; 
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

	clrscr();

	/* Time to get started initializing things! */
	printk("Initializing GDTs... ");
	gdt_install();
	printk("done\n");

	/* Load the IDT */
	printk("Initializing IDTs... ");
	idt_install();
	printk("done\n");

	/* Enable interrupts */
	printk("Initializing ISRs and enabling interrupts... ");
	enable_interrupts();
	printk("done\n");

	/* Set up the keyboard callback */
	printk("Setting up the keyboard handler... ");
	register_interrupt_handler(IRQ1, keyboard_callback);
	printk("done\n");

	/* Set up the PIT and start counting ticks */
	printk("Initializing the PIT... ");
	timer_install();
	printk("done\n");

	/* Set up paging and the kernel heap */
	printk("Initializing paging and setting up the kernel heap... ");
	init_paging();
	printk("done\n");

	printk("All initialization complete!\n\n");

	print_heap_index();

	void *a = kmalloc(8);
//	void *b = kmalloc(8);
//	void *c = kmalloc(8);

	printk("\na: %p", a);
//	printk(", b: %p\n", b);
//	printk("c: %p\n\n", c);

	printk("\n");
	print_heap_index();

	//kfree((void *)c);
	//kfree((void *)b);
	//uint32 d = kmalloc(12);
	//printk(", d: %p", d);
	//kfree((void *)d);

	/* stress test a little */
/*
#define NUM 1000
	uint32 p[NUM];
	uint32 total = 0;
//while(1) {
	total = 0;

	for (int i = 0; i < NUM; i++) {
		p[i] = kmalloc((i+1) * 32);
		total += (i+1) * 32;
		printk("alloc #%d (%d bytes, data block starts at %p)\n", i, (i+1) * 32, p[i]);

		assert(total < 30*1024*1024);
		validate_heap_index();
		//print_heap_index();
	}
	printk("%d allocs done, in total %d bytes (%d kiB)\n", NUM, total, total/1024);

	if (NUM > 100) {
		kfree((void *)p[100]);
		p[100] = NULL;
		//print_heap_index();
	}

	validate_heap_index();

	for (int i = 0; i < NUM; i++) {
		kfree((void *)p[i]);
		printk("just freed block %d (header at 0x%p)\n", i, (uint32)p[i] - sizeof(header_t));
		validate_heap_index();
		//print_heap_index();
	}
	printk("%d frees done\n", NUM);

//}
	validate_heap_index();
	print_heap_index();
	//panic("pause");

#define RAND_RANGE(x,y) ( rand() % (y - x + 1) + x )

	srand(123);

	for(int outer=1; outer != 0 ; outer++) {

	memset(p, 0, sizeof(p));
	uint32 mem_in_use = 0;
	for (int i = 0; i < 1000; i++) {
//		print_heap_index();
		uint32 r = RAND_RANGE(1,10);
		if (r >= 6) {
			uint32 r3 = RAND_RANGE(8,3268); // bytes to allocate
			printk("alloc %d bytes", r3);
			uint32 r2 =RAND_RANGE(0,1000);
			p[r2] = kmalloc(r3);
			printk(" at 0x%p\n", p[r2]);
			mem_in_use += r3;
			printk("mem in use: %d bytes (after alloc)\n", mem_in_use);
		}
		else {
			uint32 r2 = RAND_RANGE(0, 999);
//			printk("free\n");
			if (p[r2] != NULL) {
				header_t *header = (header_t *) ((uint32)p[r2] - sizeof(header_t));
				if (header->magic == HEAP_MAGIC)
					mem_in_use -= header->size;
				else
					panic("invalid magic");
			}
			kfree((void *)p[r2]);
			p[r2] = 0;
			printk("mem in use: %d bytes (after free)\n", mem_in_use);
		}
		validate_heap_index();
	}

	// Clean up
	for (int i=0; i<1000; i++) {
		kfree((void *) p[i]);
		p[i] = 0;
	}
	validate_heap_index();
}
	printk("ALL DONE!\n");

*/

/*
	printk("Creating a page fault...");
	uint32 *ptr = (uint32 *)0xa0000000;
	*ptr = 10;
*/

	printk("\n\n");
	printk("kmain() done; running infinite loop\n");
	for(;;);
/*
	Time t;
	memset(&t, 0, sizeof(t));
	get_time(&t);

	for (;;) {
		get_time(&t);
		print_time(&t);
	}
	*/
}
