#include <paging.h>
#include <gdtidt.h>
#include <types.h>
#include <string.h> /* memset */
#include <kernutil.h> /* panic */
#include <kheap.h>
#include <monitor.h> /* printk */

#define PAGE_SIZE 0x1000

// The kernel's page directory
page_directory_t *kernel_directory = 0;
// The current page directory;
page_directory_t *current_directory = 0;

/* The bitset of free/used frames */
uint32 *used_frames;
uint32 nframes;

/* kheap.c */
extern uint32 placement_address;

/* Bitmap macros */
/* 32 == sizeof(uint32) in bits */
#define INDEX_FROM_BIT(a) (a / 32)
#define OFFSET_FROM_BIT(a) (a % 32)

/******************************
 **** BITMAP HANDLING CODE ****
 ******************************/

/* Set a bit in the used_frames bitmap */
static void set_frame(uint32 frame_addr) {
	uint32 frame = frame_addr /*/ PAGE_SIZE */;
	uint32 index = INDEX_FROM_BIT(frame);
	uint32 offset = OFFSET_FROM_BIT(frame);
	used_frames[index] |= (0x1 << offset);
}

/* Clear a bit in the used_frames bitmap */
static void clear_frame(uint32 frame_addr) {
	uint32 frame = frame_addr /* / PAGE_SIZE */;
	uint32 index = INDEX_FROM_BIT(frame);
	uint32 offset = OFFSET_FROM_BIT(frame);
	used_frames[index] &= ~(0x1 << offset);
}

/* Test whether a bit is set in the used_frames bitmap */
/*
static bool test_frame(uint32 frame_addr) {
	uint32 frame = frame_addr; // frame_addr / PAGE_SIZE
	uint32 index = INDEX_FROM_BIT(frame);
	uint32 offset = OFFSET_FROM_BIT(frame);
	if ((used_frames[index] & (0x1 << offset)) != 0)
		return true;
	else
		return false;
}
*/
/* Returns the first free frame */
static uint32 first_free_frame(void) {
	uint32 index, offset;
	for (index = 0; index < INDEX_FROM_BIT(nframes); index++) {
		if (used_frames[index] == 0xffffffff) {
			/* No bits are free among the 32 tested; try the next index */
			continue;
		}

		/* Since we're still here, at least one bit among these 32 is zero... Let's find the first. */
		for (offset = 0; offset < 32; offset++) {
			if ((used_frames[index] & (1 << offset)) == 0) {
				/* Found it! Return the frame address. */
				return index * 32 + offset;
			}
		}
	}

	/* If this is reached, there were no free frames! */
	return 0xffffffff;
}

/**********************************
 **** END BITMAP HANDLING CODE ****
 **********************************/

/* Allocate a frame */
void alloc_frame(page_t *page, bool kernelmode, bool writable) {
	if (page->frame != 0) {
		/* This frame is already allocated! */
		return;
	} 
	else {
		uint32 index = first_free_frame();

		if (index == 0xffffffff) {
			panic("alloc_frame(): no frames available!");
		}

		/* Claim the frame */
		/* FIXME: this was index * PAGE_SIZE in JamesM's code, which I've modified because it appeared incorrect. */
		set_frame(index);

		page->present = 1;
		page->rw = (writable ? 1 : 0);
		page->user = (kernelmode ? 0 : 1);
		page->frame = index;
	}
}

/* Free a frame */
void free_frame(page_t *page) {
	if (page->frame == 0)
		return;

	clear_frame(page->frame);
	page->frame = 0;
}

/* Sets up everything required and activates paging. */
void init_paging() {
	/* The size of physical memory. Let's assume it's 16MB for the moment. */
	uint32 mem_end_page = 0x1000000;

	nframes = mem_end_page / PAGE_SIZE; /* divide by page size */

	/* allocate and initialize the bitmap */
	used_frames = (uint32 *)kmalloc(nframes / 32);
	memset(used_frames, 0, nframes / 32);

	/* Create a page directory */
	kernel_directory = (page_directory_t *)kmalloc_a(sizeof(page_directory_t));
	memset(kernel_directory, 0, sizeof(page_directory_t));
	current_directory = kernel_directory;

	/*
	 * Identity map from the beginning (0x0) of memory to
	 * to the end of used memory, so that we can access it
	 * as if paging wasn't enabled.

	 * Note that the loop body modifies placement_address;
	 * the while loop takes care of the comparisons, though.
	 */
	int i = 0;
	while (i < placement_address + 0x1000) {
		/* Kernel code is readable but not writable from userspace */
		/* FIXME: wat. Make this readable and understandable! */
		alloc_frame(get_page(i, true, kernel_directory), 0, 0);
		i += 0x1000;
	}

	/* Register the page fault handler */
	register_interrupt_handler(14 /* TODO: exception #defines? */, page_fault_handler);

	/* Enable paging! */
	switch_page_directory(kernel_directory);
}

/* Loads the page directory at /new/ into the CR3 register. */
void switch_page_directory(page_directory_t *dir) {
	current_directory = dir;
	asm volatile("mov %0, %%cr3;" /* set the page directory register */
			     "mov %%cr0, %%eax;"
				 "or $0x80000000, %%eax;" /* PG = 1! */
				 "mov %%eax, %%cr0"
				 : /* no outputs */
				 : "r"(& dir->tables_physical) /* FIXME: is the & correct? */
				 : "%eax");
}

/* Returns a pointer to the page entry responsible for the address at /addr/.
 * If create is true, the page table to which the page should belong
 * will be created, if it doesn't already exist.
 */
page_t *get_page (uint32 addr, bool create, page_directory_t *dir) {
	/* Turn the address into an index. */
	addr /= 0x1000;

	/* Find the page table containing this address */
	uint32 table_idx = addr / 1024;

	/* Check whether this address/page already has a table; if so, just return the page */
	if (dir->tables[table_idx] != NULL) {
		return & dir->tables[table_idx]->pages[addr % 1024];
	}
	else if (create == true) {
		/* It doesn't exist, but are told to create it if not, so let's create it. */
		uint32 phys_addr;
		/* allocate it */
		dir->tables[table_idx] = (page_table_t *)kmalloc_ap(sizeof(page_table_t), &phys_addr);
		/* zero the new table */
		memset(dir->tables[table_idx], 0, 0x1000);
		dir->tables_physical[table_idx] = phys_addr | 0x7; /* PRESENT, RW, US */ /* FIXME: why do we simply OR the full address...? */

		return & dir->tables[table_idx]->pages[addr % 1024];
	}
	else {
		/* Page doesn't already have a table, AND we shouldn't create one */
		return NULL;
	}
}

/* The page fault interrupt handler. */
void page_fault_handler(registers_t regs) {
	/* Whenever a page fault occurs, the CR2 register contains the fault address. */
	uint32 faulting_address;
	asm volatile("mov %%cr2, %0" : "=r" (faulting_address));

	/* We also get an error code pushed to the stack, located in regs.err_code. */
	bool present = ! (regs.err_code & (1 << 0)); // page wasn't present if this is true
	bool write = regs.err_code & (1 << 1);       // was the access a write? true if write, false if read
	bool user = regs.err_code & (1 << 2);        // did the access happen from user mode (ring 3) or kernel mode (ring 0)?   
	bool reserved = regs.err_code & (1 << 3);    // was the fault caused by us setting a reserved bit to 1 in entry?
	bool int_fetch = regs.err_code & (1 << 4);   // was the fault caused by an instruction fetch?

	/* Print a message and panic */
	printk("Page fault!\n"
		   "Flags set: %s %s %s %s %s\n"
		   "Faulting address: 0x%x\n", 
		   (present ? "page_not_present" : ""),
		   (write   ? "action=write" : "action=read"),
		   (user    ? "user-mode" : "kernel-mode"),
		   (reserved? "reserved_bits_trampled" : ""),
		   (int_fetch?"int_fetch" : ""),
		   faulting_address);
	panic("Page fault");
}
