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
#include <kernel/rtc.h>

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


	/**********************************
	 *** HEAP DEBUGGING AND TESTING ***
	 **********************************/

	print_heap_index();

	void *a = kmalloc(8);
	void *b = kmalloc(8);
	void *c = kmalloc(8);

	printk("\na: %p", a);
	printk(", b: %p\n", b);
	printk("c: %p\n", c);

	printk("\n");
	print_heap_index();

	printk("Freeing c...\n");
	kfree(c);
	print_heap_index();
	printk("Freeing a...\n");
	kfree(a);
	print_heap_index();
	printk("Freeing b...\n");
	kfree(b);
	print_heap_index();

	/* The highest address allocated in the stress tests; stored for testing purposes, of course */
	void *max_alloc = NULL;

	/* stress test a little */
#define NUM 1250
	void *p[NUM];
	uint32 total = 0;
//while(1) {
	total = 0;

	for (int i = 0; i < NUM; i++) {
		p[i] = kmalloc((i+1) * 32);
		if (p[i] > max_alloc) max_alloc = p[i];
		total += (i+1) * 32;
		printk("alloc #%d (%d bytes, data block starts at %p)\n", i, (i+1) * 32, p[i]);

		assert(total < 30*1024*1024);
		validate_heap_index(false);
		//print_heap_index();
	}
	printk("%d allocs done, in total %d bytes (%d kiB)\n", NUM, total, total/1024);

	if (NUM > 100) {
		kfree((void *)p[100]);
		p[100] = NULL;
	}

	validate_heap_index(false);

	for (int i = 0; i < NUM; i++) {
		kfree((void *)p[i]);
		printk("just freed block %d (header at 0x%p)\n", i, (uint32)p[i] - sizeof(area_header_t));
		validate_heap_index(false);
		//print_heap_index();
	}
	printk("%d frees done\n", NUM);

//}
	validate_heap_index(false);
	print_heap_index();
	//panic("pause");

/****************************
  *** STRESS TEST PART II ***
  ***************************/

#define RAND_RANGE(x,y) ( rand() % (y - x + 1) + x )

#define NUM_OUTER_LOOPS 3

	srand(123);

	for(int outer=1; outer <= NUM_OUTER_LOOPS  ; outer++) {

	memset(p, 0, sizeof(p));
	uint32 mem_in_use = 0;
	for (int i = 0; i < NUM; i++) {
//		print_heap_index();
		uint32 r = RAND_RANGE(1,10);
		if (r >= 6) {
			uint32 r2 =RAND_RANGE(0,NUM-1);
			if (p[r2] != NULL) {
				/* This area was used already; don't overwrite or we can't free it! */
				continue;
			}
			uint32 r3 = RAND_RANGE(8,3268); // bytes to allocate
			if (r3 > 3200)
					r3 *= 200; /* test */
			printk("alloc %d bytes", r3);
			p[r2] = kmalloc(r3);
		if (p[r2] > max_alloc) max_alloc = p[r2];
			printk(" at 0x%p\n", p[r2]);
			mem_in_use += r3 + sizeof(area_header_t) + sizeof(area_footer_t);
			printk("mem in use: %d bytes (after alloc)\n", mem_in_use);
		}
		else {
			uint32 r2 = RAND_RANGE(0, NUM-1);
//			printk("free\n");
			if (p[r2] != NULL) {
				area_header_t *header = (area_header_t *) ((uint32)p[r2] - sizeof(area_header_t));
				if (header->magic == HEAP_MAGIC)
					mem_in_use -= header->size;
				else
					panic("invalid magic");
			}
			kfree((void *)p[r2]);
			p[r2] = 0;
			printk("mem in use: %d bytes (after free)\n", mem_in_use);
		}
		validate_heap_index(false);
	}

	// Clean up
	for (int i = 0; i < NUM; i++) {
		if (p[i] != NULL) {
			area_header_t *header = (area_header_t *) ((uint32)p[i] - sizeof(area_header_t));
			if (header->magic == HEAP_MAGIC)
				mem_in_use -= header->size;
			else
				panic("invalid magic");
			printk("mem in use: %d bytes (after free)\n", mem_in_use);
			kfree((void *)p[i]);
			p[i] = 0;
		}
	}
	validate_heap_index(false);
}
print_heap_index();
	printk("ALL DONE! max_alloc = %p\n", max_alloc);

/*
	printk("Creating a page fault...");
	uint32 *ptr = (uint32 *)0xa0000000;
	*ptr = 10;
*/

	printk("\n\n");
	printk("kmain() done; running infinite loop\n");
//	for(;;);
	Time t;
	memset(&t, 0, sizeof(t));
	get_time(&t);

	for (;;) {
		get_time(&t);
		print_time(&t);
	}
}
