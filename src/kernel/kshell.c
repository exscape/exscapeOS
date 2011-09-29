#include <types.h>
#include <kernel/kshell.h>
#include <kernel/monitor.h>
#include <kernel/keyboard.h>
#include <kernel/kheap.h>
#include <string.h>
#include <kernel/kernutil.h>

/* for heaptest() */
#include <kernel/paging.h>
#include <kernel/timer.h>
#include <stdlib.h>

void heaptest(void);

void kshell(void) {
	unsigned char *buf = kmalloc(1024);
	memset(buf, 0, 1024);

	while (true) {
		printk("kshell # ");
		unsigned char ch;
		uint32 i = 0;
		while ( (ch = getchar()) != '\n') {
			assert(i < 1023);

			if (ch != 0x08) {
				/* if not backspace */
				buf[i++] = ch;
			}
			else if (i != 0) {
				/* backspace */
				i--;
			}
			else
				continue;

			putchar(ch);

			if (ch == 0x08) {
				putchar(' '); cursor.x--;
			}
			
			update_cursor();
		}
		buf[i] = 0;

		putchar('\n');

		//printk("\ngetchar loop done, command entered: \"%s\"\n", buf);

		/* TODO: trim() or such */

		if (strcmp((char *)buf, "heaptest") == 0) {
			heaptest();
		}
		else if (strcmp((char *)buf, "ls") == 0) {
			//ls_initrd();
		}
		else if (strcmp((char *)buf, "help") == 0) {
			printk("exscapeOS kernel shell help\n\nAvailable commands:\n");
			printk("heaptest: launch the heap stress test\n");
			printk("ls: show the files on the initrd image\n");
			printk("help: show this help message\n");
		}

	}
}

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

void heaptest(void) {
	/**********************************
	 *** HEAP DEBUGGING AND TESTING ***
	 **********************************/

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
	assert(IS_PAGE_ALIGNED(aligned));

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
//#define NUM 2750

	/* account for the overhead and a tiny bit more */
	uint32 free = free_bytes();
	free -= ((free/1024/128) + 24) * (sizeof(area_header_t) + sizeof(area_footer_t));


	/* This MUST be const and MUST NOT change (especially not being INCREASED in size) later!
	 * If this is increased, accessing p[] outside of NUM-1 will cause invalid memory accesses (duh!) */
	const uint32 NUM = ((free / 1024 / 128) - 8); /* we allocate 128k at a time; reduce slighly to not run out of frames */

	/* Make sure it didn't overflow or anything... not exactly a rigorous test; whatever */
	assert (NUM > 0 && NUM < 0xffffff00);

	void **p = kmalloc(sizeof(void *) * NUM);
	memset(p, 0, sizeof(void *) * NUM);

	/* store the alloc'ed size of all areas; we can't read the header safely, because alloc() may resize the area AFTER allocation!
	 * this would make verification of contents complain in error, because the NEW space wouldn't be filled with test bytes! */
	uint32 *alloced_size = kmalloc(sizeof(uint32) * NUM);
	memset(alloced_size, 0, sizeof(uint32) * NUM);

	uint32 total = 0;
//while(1) {
	total = 0;

	for (uint32 i = 0; i < NUM; i++) {
		p[i] = kmalloc(128 * 1024);
		if (p[i] > max_alloc) max_alloc = p[i];
		total += 128 * 1024;
		printk("alloc #%d (%d bytes, data block starts at %p)\n", i, 128 * 1024, p[i]);

		validate_heap_index(false);
		//print_heap_index();
	}
	printk("%d allocs done, in total %d bytes (%d kiB)\n", NUM, total, total/1024);

	/* Free one in "the middle" */
	if (NUM > 100) {
		kfree((void *)p[100]);
		p[100] = NULL;
	}

	validate_heap_index(false);

	for (uint32 i = 0; i < NUM; i++) {
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
	for (uint32 i = 0; i < NUM; i++) {

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
	for (uint32 i = 0; i < NUM; i++) {
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
}
