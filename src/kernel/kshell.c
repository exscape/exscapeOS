#include <types.h>
#include <kernel/kshell.h>
#include <kernel/console.h>
#include <kernel/keyboard.h>
#include <kernel/kheap.h>
#include <string.h>
#include <kernel/kernutil.h>
#include <kernel/task.h>
#include <kernel/list.h>
#include <kernel/syscall.h>

/* for heaptest() */
#include <kernel/paging.h>
#include <kernel/timer.h>
#include <stdlib.h>

/* for ls_initrd() */
#include <kernel/initrd.h>

/* for ps */
extern volatile task_t *current_task;
extern task_t kernel_task;

void heaptest(void *data, uint32 length);
void ls_initrd(void *data, uint32 length);

extern volatile list_t ready_queue;

static void infinite_loop(void *data, uint32 length) {
	for(;;);
}

static void create_pagefault(void *data, uint32 length) {
	uint32 *pf = (uint32 *)0xffff0000;
	*pf = 10;
}

static void create_pagefault_delay(void *data, uint32 length) {
	sleep(2000);
	create_pagefault(NULL, 0);
}

static void print_1_sec(void *data, uint32 length) {
	for (int i=0; i < 10; i++) {
		printk("print_1_sec: loop %d of 10\n", i+1);
		sleep(1000);
	}
	printk("print_1_sec exiting\n");
}

static void fill_scrollback(void *data, uint32 length) {
	for (int i=1; i <= MAX_SCROLLBACK * 2; i++) {
		printk("%d\n", i);
		if (i > 300)
			delay(40);
	}
}

static void fpu_task(void *data, uint32 length) {
	asm volatile("fldpi");
}

static void guess_num(void *data, uint32 length) {
	/* A simple "guess the number" game. 1-digit number due to the lack of simple library functions
	 * for keyboard input. */

	int num = RAND_RANGE(0, 9);
	int guess = -1;
	int num_guesses = 0;

	while (guess != num) {
		num_guesses++;
		printk("Guess the number (0-9): ");
		guess = getchar();
		printk("%c  ", guess);
		guess -= 0x30; /* ASCII to num */
		if (guess == num) {
			printk("You got it! I was looking for %d.\nIt took you %d guesses to find it.\n", num, num_guesses);
			break;
		}
		else if (guess > num) {
			printk("Nope. Try lower.\n");
		}
		else if (guess < num) {
			printk("Nope. Try higher.\n");
		}
	}
}

static void paramtest(void *data, uint32 length) {
	printk("data=0x%08x length=0x%08x\n", data, length);
}

static void user_test(void *data, uint32 length) {
	/* A task that runs in user mode */
	syscall_puts("Hellooooooo, USER MODE WORLD!");
}


static void divzero(void *data, uint32 length) {
	printk("in divzero; dividing now\n");
	asm volatile("mov $10, %%eax; mov $0, %%ebx; div %%ebx;" : : : "%eax", "%ebx", "%edx");
	printk("divzero: after dividing\n");
}

static void delaypanic(void *data, uint32 length) {
	sleep(5000);
	panic("delaypanic()");
}

static void delaymput(void *data, uint32 length) {
	sleep(5000);
	Point p = get_cursor();
	set_cursor((80-4)/2, 12);
	printk("test");
	set_cursor(p.x, p.y);
}

static void testbench(void *data, uint32 length) {
	/* An extremely simple "benchmark" to test approx. how much CPU time a task is getting */
	uint32 start_tick = gettickcount();
	printk("start testbench at tick %u...\n", start_tick);
	for (int i = 100000000; i != 0; i--) {
		int a = i * 10 + 4;
		i |= (a & 0); /* nop */
	}
	uint32 end_tick = gettickcount();
	printk("finish testbench at tick %u; time taken: %u ticks (%u ms)\n", end_tick, (end_tick - start_tick), (end_tick - start_tick)*10);
}

static void permaidle(void *data, uint32 length) {
	printk("permaidle task launched. no further output will be generated\n");
	for(;;) {
		sleep(100000);
	}
}

static void free(void *data, uint32 length) {
	printk("Free RAM: %u bytes\n", free_bytes());
	printk("kheap used: %u bytes\n", kheap_used_bytes());
}

static void sleep_test(void *data, uint32 length) {
	printk("sleep_test: sleeping for 20 seconds\n");
	sleep(20000);
	printk("sleep test done\n");
}

void kshell(void *data, uint32 length) {
	unsigned char *buf = kmalloc(1024);
	memset(buf, 0, 1024);
	char *last_cmd = kmalloc(1024);
	memset(last_cmd, 0, 1024);

	task_t *task = NULL;

	/* Make sure the current code spawns a new task for the kernel shell; otherwise, sleep() won't work. */
	assert(current_task != &kernel_task);
	assert(strlen((char *)current_task->name) >= 6 && strncmp((char *)current_task->name, "kshell", 6) == 0);

	while (true) {

		/* Don't "return" to a new shell prompt until the current task in finished.
		 * Due to how the scheduler works and the lack of a HLT task (that doesn't use 1/num_processes CPU *constantly*),
		 * this will use 100% CPU if no tasks use the CPU.
		 */
		while (task != NULL) {
			if (does_task_exist(task) == false) {
				task = NULL;
				break;
			}
			sleep(10);
		}

		printc(BLACK, GREEN, "kshell ");
		printc(BLACK, RED, "# ");
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
				putchar(' '); cursor_left();
			}

			update_cursor();
		}
		buf[i] = 0;

		putchar('\n');

		/* The console to give new tasks */
		console_t *con = current_task->console;

		char *p = trim((char *)buf);
		if (strcmp(p, "!!") == 0 && *last_cmd != 0) {
			/* fetch the last command run */
			strlcpy(p, last_cmd, 1024);
		}

		if (strcmp(p, "heaptest") == 0) {
			task = create_task(&heaptest, "heaptest", con, NULL, 0);
		}
		if (strcmp(p, "free") == 0) {
			task = create_task(&free, "free", con, NULL, 0);
		}
		else if (strcmp(p, "delaypanic") == 0) {
			task = create_task(&delaypanic, "delaypanic", con, NULL, 0);
		}
		else if (strcmp(p, "fill_scrollback") == 0) {
			task = create_task(&fill_scrollback, "fill_scrollback", con, NULL, 0);
		}
		else if (strcmp(p, "ls") == 0) {
			ls_initrd(NULL, 0);
		}
		else if (strcmp(p, "print_heap") == 0) {
			validate_heap_index(true);
		}
		else if (strcmp(p, "clear") == 0) {
			clrscr();
		}
		else if (strcmp(p, "reboot") == 0) {
			reboot();
		}
		else if (strcmp(p, "paramtest") == 0) {
			task = create_task(&paramtest, "paramtest", con, (void *)0xDEADBEEF, 0x456);
		}
		else if (strcmp(p, "exit") == 0) {
			break;
		}
		else if (strcmp(p, "print_1_sec") == 0) {
			task = create_task(&print_1_sec, "print_1_sec", con, NULL, 0);
		}
		else if (strcmp(p, "sleeptest") == 0) {
			task = create_task(&sleep_test, "sleeptest", con, NULL, 0);
		}
		else if (strcmp(p, "fpu_task") == 0) {
			task = create_task(&fpu_task, "fpu_task", con, NULL, 0);
		}
		else if (strcmp(p, "permaidle") == 0) {
			task = create_task(&permaidle, "permaidle", con, NULL, 0);
		}
		else if (strcmp(p, "pagefault") == 0) {
			task = create_task(&create_pagefault, "create_pagefault", con, NULL, 0);
		}
		else if (strcmp(p, "pagefault_delay") == 0) {
			task = create_task(&create_pagefault_delay, "create_pagefault_delay", con, NULL, 0);
		}
		else if (strcmp(p, "uptime") == 0) {
			uint32 up = uptime();
			uint32 ticks = gettickcount();
			printk("Uptime: %u seconds (%u ticks)\n", up, ticks);
		}
		else if (strcmp(p, "infloop_task") == 0) {
			task = create_task(&infinite_loop, "infinite_loop", con, NULL, 0);
		}
		else if (strcmp(p, "reset") == 0) {
			reset();
		}
		else if (strcmp(p, "ps") == 0) {
			node_t *cur_task_node = ready_queue.head;
			int n = 0;
			printk("%5s %10s %10s %6s %s\n", "PID", "STACK_BTM", "PAGEDIR", "STATE", "NAME");
			while (cur_task_node != NULL) {
				task_t *cur_task = (task_t *)cur_task_node->data;
				n++;
				printk("% 5d 0x%08x 0x%08x %06s %s\n", cur_task->id, cur_task->stack, cur_task->page_directory, (cur_task->state == TASK_RUNNING ? "RUN" : (cur_task->state == TASK_SLEEPING ? "SLEEP" : (cur_task->state == TASK_IOWAIT ? "IOWAIT" : "UNKN."))), cur_task->name);
/*				printk("PID: %d\nNAME: %s\nstack start (highest address): 0x%08x\npage dir: 0x%08x\nstate: %s\n", cur_task->id, cur_task->name, cur_task->stack, cur_task->page_directory,
						(cur_task->state == TASK_RUNNING ? "RUNNING" : (cur_task->state == TASK_SLEEPING ? "SLEEPING" : "UNKNOWN")));

						*/
#if 0
				/* Doesn't work in VMware/Parallels! The alignment is off somehow, so all the values are wrong... */
				if (current_task != cur_task) {
					registers_t *regs = (registers_t *)( (uint32)cur_task->esp );
					assert(regs->ds == 0x10 || regs->ds == 0x23);
					assert(regs->cs == 0x08 || regs->cs == 0x1b);
					printk("EAX=%08x    EBX=%08x    ECX=%08x    EDX=%08x\n", regs->eax, regs->ebx, regs->ecx, regs->edx);
					printk("ESI=%08x    EDI=%08x    ESP=%08x    EBP=%08x\n", regs->esi, regs->edi, cur_task->esp, regs->ebp);
					printk("CS =%08x    EIP=%08x    EFLAGS=%08x USERESP=%08x\n", regs->cs, regs->eip, regs->eflags, regs->useresp);
				}
#endif

//				printk("--------------\n");

				cur_task_node = cur_task_node->next;
			}
			printk("%d tasks running\n", n);
		}
		else if(strcmp(p, "divzero") == 0) {
			divzero(NULL, 0);
		}
		else if (strcmp(p, "guess") == 0) {
			task = create_task(&guess_num, "guess_num", con, NULL, 0);
		}
		else if(strcmp(p, "divzero_task") == 0) {
			task = create_task(&divzero, "divzero", con, NULL, 0);
		}
		else if (strcmp(p, "delaymput") == 0) {
			task = create_task(&delaymput, "delaymput", con, NULL, 0);
		}
		else if (strncmp(p, "kill ", 5) == 0) {
			p += 5;

			/* p now points to the argument */
			int pid = atoi(p);
			assert(pid > 0);
			if (kill_pid(pid)) {
				printk("killed task with PID %d\n", pid);
			}
			else {
				printk("unable to kill task with PID %d; task not found?\n", pid);
			}
		}
		else if (strcmp(p, "testbench") == 0) {
			testbench(NULL, 0);
		}
		else if (strcmp(p, "testbench_task") == 0) {
			task = create_task(&testbench, "testbench", con, NULL, 0);
		}
		else if (strcmp(p, "user_test") == 0) {
			/* launch a user mode test task */
			task = create_task_user(&user_test, "user_test", con, NULL, 0);
		}
		else if (strcmp(p, "kshell") == 0) {
			/* Heh. For testing only, really... Subshells aren't high in priority for the kernel shell. */
			task = create_task(&kshell, "kshell (nested)", con, NULL, 0);
		}
		else if (strcmp(p, "") == 0) {
			/* do nothing */
		}
		else if (strcmp(p, "help") == 0) {
			printk("exscapeOS kernel shell help\n\nAvailable commands:\n");
			printk("heaptest: launch the heap stress test\n");
			printk("print_heap: print the heap usage map\n");
			printk("ls: show the files on the initrd image\n");
			printk("clear: clear the screen\n");
			printk("reboot: reboots the system\n");
			printk("pagefault: generate a page fault and crash\n");
			printk("divzero: divide by zero after setting most registers to test values\n");
			printk("uptime: display the current uptime\n");
			printk("ps: basic info about the running tasks\n");
			printk("kill <pid>: kill a task\n");
			printk("testbench, testbench_task: run a simple benchmark, in-kernel or as a task\n");
			printk("sleeptest, permaidle: launch tasks that sleep 20 seconds once/sleep 100 seconds in a loop\n");
			printk("print_1_sec: launch a task that loops 10 times, printing a message and then sleeping 1 second\n");
			printk("delaypanic: wait 5 seconds, then panic\n");
			printk("help: show this help message\n");
		}
		else {
			printk("Unknown command: \"%s\"\n", p);
		}

		strlcpy(last_cmd, p, 1024);
	}
}

/* Used for heap debugging only. Verifies that the area from /p/ to /p + size/ 
 * is filled with 0xaa bytes (to make sure the area isn't overwritten by something). */
static void verify_area(void *in_p, uint32 size) {
	unsigned char *ptr;
	for (ptr = in_p; ptr < ((unsigned char *)in_p + size); ptr++) {
		if (*ptr != 0xaa) {
			print_heap_index();
			printk("ERROR: data at %p isn't 0xaa! Heap index for just now is above.\n", ptr);
			panic("Data was overwritten!");
		}
	}
}

void heaptest(void *data, uint32 length) {
	/**********************************
	 *** HEAP DEBUGGING AND TESTING ***
	 **********************************/

#define TEST_1_LOOPS 1
#define TEST_2_LOOPS 1

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

	/* The highest address allocated in the stress tests; stored for testing purposes, of course */
	void *max_alloc = NULL;

	/* stress test a little */
//#define NUM 2750

	/* account for the overhead and a tiny bit more */
	uint32 free_ = free_bytes();
	free_ -= ((free_/1024/128) + 24) * (sizeof(area_header_t) + sizeof(area_footer_t));


	/* This MUST be const and MUST NOT change (especially not being INCREASED in size) later!
	 * If this is increased, accessing p[] outside of NUM-1 will cause invalid memory accesses (duh!) */
	const uint32 NUM = ((free_ / 1024 / 128) - 8); /* we allocate 128k at a time; reduce slighly to not run out of frames */

	/* Make sure it didn't overflow or anything... not exactly a rigorous test; whatever */
	assert (NUM > 0 && NUM < 0xffffff00);

	void **p = kmalloc(sizeof(void *) * NUM);
	memset(p, 0, sizeof(void *) * NUM);

	/* store the alloc'ed size of all areas; we can't read the header safely, because alloc() may resize the area AFTER allocation!
	 * this would make verification of contents complain in error, because the NEW space wouldn't be filled with test bytes! */
	uint32 *alloced_size = kmalloc(sizeof(uint32) * NUM);
	memset(alloced_size, 0, sizeof(uint32) * NUM);

	uint32 total = 0;
	for (int x=0; x < TEST_1_LOOPS; x++) {
	total = 0;

	for (uint32 i = 0; i < NUM; i++) {
		uint32 sz = RAND_RANGE(4, 65535);
		p[i] = kmalloc(sz);
		if (p[i] > max_alloc) max_alloc = p[i];
		total += sz;
		printk("alloc #%d (%d bytes, data block starts at %p)\n", i, sz, p[i]);

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

}
	validate_heap_index(false);
	print_heap_index();

/****************************
  *** STRESS TEST PART II ***
  ***************************/

	uint32 num_allocs = 0; /* just a stats variable, to print later */
	uint32 kbytes_allocated = 0;

	srand(1234567);
	printk("Running the large stress test\n");

	for(int outer=1; outer <= TEST_2_LOOPS ; outer++) {
		printk("\nloop %d/%d\n", outer, TEST_2_LOOPS);

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
//print_heap_index();
	printk("ALL DONE! max_alloc = %p; total number of allocations: %d\n", max_alloc, num_allocs);
	printk("Total allocated: (approx.) %u kiB (%u MiB)\n", kbytes_allocated, kbytes_allocated >> 10);

#if 0
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
#endif
}

void ls_initrd(void *data, uint32 length) {
	int ctr = 0;
	struct dirent *node = NULL;
	while ( (node = readdir_fs(fs_root, ctr)) != 0) {
		fs_node_t *fsnode = finddir_fs(fs_root, node->d_name);
		printk("Found %s: %s\n", ((fsnode->flags & 0x7) == FS_DIRECTORY) ? "directory" : "file", node->d_name);
		if (fsnode->flags == FS_FILE) {
			unsigned char buf[256];
			uint32 sz = read_fs(fsnode, 0, 256, buf);
			buf[sz] = 0;
			printk("    contents: \"%s\"\n", buf);
		}

		ctr++;
	}
}
