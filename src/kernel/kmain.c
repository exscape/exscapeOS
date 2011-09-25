#include <types.h>
#include <stdlib.h> /* itoa(), reverse() */
#include <string.h> /* memset(), strlen() */
#include <kernel/kernutil.h> /* inb, inw, outw */
#include <kernel/monitor.h> /* printing, scrolling etc. */
#include <kernel/gdt.h>
#include <kernel/interrupts.h>
#include <stdio.h>
#include <kernel/keyboard.h>
#include <kernel/timer.h>
#include <kernel/kheap.h>
#include <kernel/paging.h>
#include <kernel/rtc.h>
#include <kernel/multiboot.h>
#include <kernel/initrd.h>
#include <kernel/task.h>
#include <kernel/syscall.h>

/* Used for heap debugging only. Verifies that the area from /p/ to /p + size/ 
 * is filled with 0xaa bytes (to make sure the area isn't overwritten by something). */
void verify_area(void *in_p, uint32 size) {
	unsigned char *ptr;
	for (ptr = in_p; ptr < ((unsigned char *)in_p + size); ptr++) {
		if (*ptr != 0xaa) {
			print_heap_index();
			printk("ERROR: data at %p isn't 0xaa! Heap index for just now is above.\n", ptr);
			panic("Data was overwritten!");
		}
	}
}

/* kheap.c */
extern uint32 placement_address;

void kmain(multiboot_info_t *mbd, unsigned int magic) {
	if (magic != 0x2BADB002) {
		panic("Invalid magic received from bootloader!");
	}

	if (mbd->mods_count == 0) {
		panic("initrd.img not loaded! Make sure the GRUB config contains a \"module\" line.\nSystem halted.");
	}

	/* This must be done before anything below (GDTs, etc.), since kmalloc() may overwrite the initrd otherwise! */
	uint32 initrd_location = *((uint32 *)mbd->mods_addr);
	uint32 initrd_end = *((uint32 *)(mbd->mods_addr + 4));
	if (initrd_end > placement_address)
		placement_address = initrd_end;

	/* This should be done EARLY on, since many other things will fail (possibly even panic() output) otherwise. */
	init_video();

	if (mbd->flags & 1) {
		printk("Memory info (thanks, GRUB!): %u kiB lower, %u kiB upper\n",
			mbd->mem_lower, mbd->mem_upper);
	}
	else
		panic("mbd->flags bit 0 is unset!");

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

	/* Initialize the initrd */
	/* (do this before paging, so that it doesn't end up in the kernel heap) */
	fs_root = init_initrd(initrd_location);

	/* Set up paging and the kernel heap */
	printk("Initializing paging and setting up the kernel heap... ");
	init_paging(mbd->mem_upper);
	printk("done\n");

	/* Set up the syscall interface */
	printk("Initializing syscalls... ");
	init_syscalls();
	printk("done\n");

	printk("All initialization complete!\n\n");

	switch_to_user_mode();
	asm volatile("hlt");

	return;






	int ctr = 0;
	struct dirent *node = NULL;
	while ( (node = readdir_fs(fs_root, ctr)) != 0) {
		fs_node_t *fsnode = finddir_fs(fs_root, node->name);
		printk("Found %s: %s\n", ((fsnode->flags & 0x7) == FS_DIRECTORY) ? "directory" : "file", node->name);
		if (fsnode->flags == FS_FILE) {
			unsigned char buf[256];
			uint32 sz = read_fs(fsnode, 0, 256, buf);
			buf[sz] = 0;
			printk("    contents: \"%s\"\n", buf);
		}

		ctr++;
	}


	//for(;;);


	/**********************************
	 *** HEAP DEBUGGING AND TESTING ***
	 **********************************/
#if 1

	print_heap_index();

	void *a = kmalloc(8);
	void *b = kmalloc(8);
	void *c = kmalloc(8);

	memset(a, 0xab, 6);
	memcpy(b, a, 6);

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

	printk("Testing page alignment...");

	void *initial = kmalloc(16); /* to make sure the alignments aren't aligned by chance */
	printk("Initial: %p\n", initial);
	print_heap_index();
	void *unaligned = kmalloc(16);
	printk("Unaligned: %p\n", unaligned);
	print_heap_index();
	void *aligned = kmalloc_a(16);
	printk("Aligned: %p\n", aligned);
	print_heap_index();

	printk("Freeing them all...\n");
	kfree(initial);
	kfree(unaligned);
	kfree(aligned);
	print_heap_index();

//	printk("Infinite loop...\n");
//	for(;;);

	/* The highest address allocated in the stress tests; stored for testing purposes, of course */
	void *max_alloc = NULL;

	/* stress test a little */
#define NUM 2750
	void **p = kmalloc(sizeof(void *) * NUM);
	memset(p, 0, sizeof(void *) * NUM);

	/* store the alloc'ed size of all areas; we can't read the header safely, because alloc() may resize the area AFTER allocation!
	 * this would make verification of contents complain in error, because the NEW space wouldn't be filled with test bytes! */
	uint32 *alloced_size = kmalloc(sizeof(uint32) * NUM);
	memset(alloced_size, 0, sizeof(uint32) * NUM);

	uint32 total = 0;
//while(1) {
	total = 0;

	for (int i = 0; i < NUM; i++) {
		p[i] = kmalloc((i+1) * 32);
		if (p[i] > max_alloc) max_alloc = p[i];
		total += (i+1) * 32;
		printk("alloc #%d (%d bytes, data block starts at %p)\n", i, (i+1) * 32, p[i]);

		assert(total < mbd->mem_upper - 20*1024*1024);
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
		p[i] = 0;
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

#define NUM_OUTER_LOOPS 1
	uint32 num_allocs = 0; /* just a stats variable, to print later */
	uint32 kbytes_allocated = 0;

	srand(1234567);
	printk("Running the large stress test\n");

	for(int outer=1; outer <= NUM_OUTER_LOOPS  ; outer++) {
		printk("\nloop %d/%d\n", outer, NUM_OUTER_LOOPS);

	/*
	 * This code is a damn mess, but i won't bother making this easily readable,
	 * as it's temporary.
	 * It randomizes whether it should allocate or free, and whether the allocations
	 * should be page-aligned or not.
	 * It then fills its allocated space with a bit pattern, and ensures that
	 * it doesn't get overwritten.
	 */

	memset(p, 0, sizeof(p));
	uint32 mem_in_use = 0;
	for (int i = 0; i < NUM; i++) {

		if (num_allocs % 50 == 0)
			printk(".");
//		print_heap_index();
		uint32 r = RAND_RANGE(1,10);
		if (r >= 6) {
			uint32 r2 =RAND_RANGE(0,NUM-1);
			if (p[r2] != NULL) {
				/* This area was used already; don't overwrite it, or we can't free it! */
				continue;
			}
			uint32 r3 = RAND_RANGE(8,3268); // bytes to allocate
			if (r3 > 3200)
					r3 *= 200; /* test */

			uint8 align = RAND_RANGE(1, 10); /* align ~1/10 allocs */
			//printk("alloc %d bytes%s", r3, align == 1 ? ", page aligned" : "");
			if (align == 1)
				p[r2] = kmalloc_a(r3);
			else
				p[r2] = kmalloc(r3);
			//printk(" at 0x%p\n", p[r2]);

			/* store the alloc size */
			alloced_size[r2] = r3;

			num_allocs++;
			kbytes_allocated += r3/1024;

			assert(r3 != 0 && alloced_size[r2] != 0);

			/* fill the allocation with a bit pattern, to ensure that nothing overwrites it */
			memset(p[r2], 0xaa, r3);

		if (p[r2] > max_alloc) max_alloc = p[r2];
			mem_in_use += r3 + sizeof(area_header_t) + sizeof(area_footer_t);
			//printk("mem in use: %d bytes (after alloc)\n", mem_in_use);
		}
		else {
			uint32 r2 = RAND_RANGE(0, NUM-1);
			if (p[r2] != NULL) {
				mem_in_use -= alloced_size[r2];

				assert(alloced_size[r2] != 0);
				verify_area(p[r2], alloced_size[r2]);
			}
			//printk("freeing memory at %p (%d bytes)\n", p[r2], alloced_size[r2]);
			alloced_size[r2] = 0;
			kfree((void *)p[r2]);
			p[r2] = 0;
			//printk("mem in use: %d bytes (after free)\n", mem_in_use);
		}
		validate_heap_index(false);
	}

	// Clean up
	for (int i = 0; i < NUM; i++) {
		if (p[i] != NULL) {
			mem_in_use -= alloced_size[i];
			//printk("mem in use: %d bytes (after free)\n", mem_in_use);
			assert(alloced_size[i] != 0);
			verify_area(p[i], alloced_size[i]);
			kfree((void *)p[i]);
			p[i] = 0;
		}
	}
	validate_heap_index(false);
}
printk("\n");
print_heap_index();
	printk("ALL DONE! max_alloc = %p; total number of allocations: %d\n", max_alloc, num_allocs);
	printk("Total allocated: (approx.) %u kiB (%u MiB)\n", kbytes_allocated, kbytes_allocated >> 10);

	printk("Benchmarking...\n");
	void **mem = kmalloc(3250 * sizeof(void *));
	memset(mem, 0, sizeof(void *) * 3250);
	/* alloc BEFORE the benchmark! */
	uint32 start_ticks = gettickcount();
	for (int i = 0; i < 3250; i++) {
		mem[i] = kmalloc(16);
	}
	for (int i = 0; i < 3250; i++) {
		kfree(mem[i]);
		mem[i] = NULL;
	}
	uint32 end_ticks = gettickcount();
	kfree(mem);
	mem = 0;
	printk("Time taken: %d ticks (%d ms)\n", end_ticks - start_ticks, (end_ticks - start_ticks) * 10);



/*
	printk("Creating a page fault...");
	uint32 *ptr = (uint32 *)0xa0000000;
	*ptr = 10;
*/

	printk("\n\n");
	printk("kmain() done; running infinite loop\n");
//	for(;;);

#endif
	Time t;
	memset(&t, 0, sizeof(t));
	get_time(&t);

	for (;;) {
		get_time(&t);
		print_time(&t);
		asm volatile("hlt"); /* Since the PIT fires every 10ms or so, sleep the CPU until then. QEMU CPU usage goes from 100% to 4% with this line. */
	}
}
