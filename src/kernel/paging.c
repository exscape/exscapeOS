#include <kernel/paging.h>
#include <kernel/gdtidt.h>
#include <types.h>
#include <string.h> /* memset */
#include <kernel/kernutil.h> /* panic */
#include <kernel/kheap.h>
#include <kernel/monitor.h> /* printk */

// The kernel's page directory
page_directory_t *kernel_directory = 0;
// The current page directory;
page_directory_t *current_directory = 0;

/* The bitset of free/used frames */
uint32 *used_frames;
uint32 nframes;

/* defined in kheap.c */
extern uint32 placement_address;
extern heap_t *kheap;

/* Bitmap macros */
/* 32 == sizeof(uint32) in bits, so these simply calculate which dword a bit belongs to,
 * and the number of bits to shift that dword to find it, respectively. */
#define INDEX_FROM_BIT(a) (a / 32)
#define OFFSET_FROM_BIT(a) (a % 32)

/******************************
 **** BITMAP HANDLING CODE ****
 ******************************/

/* Set a bit in the used_frames bitmap */
static void set_frame(uint32 frame_addr) {
	uint32 frame = frame_addr;
	uint32 index = INDEX_FROM_BIT(frame);
	uint32 offset = OFFSET_FROM_BIT(frame);
	used_frames[index] |= (0x1 << offset);
}

/* Clear a bit in the used_frames bitmap */
static void clear_frame(uint32 frame_addr) {
	uint32 frame = frame_addr;
	uint32 index = INDEX_FROM_BIT(frame);
	uint32 offset = OFFSET_FROM_BIT(frame);
	used_frames[index] &= ~(0x1 << offset);
}

/* Test whether a bit is set in the used_frames bitmap */
static bool test_frame(uint32 frame_addr) {
	uint32 frame = frame_addr;
	uint32 index = INDEX_FROM_BIT(frame);
	uint32 offset = OFFSET_FROM_BIT(frame);
	if ((used_frames[index] & (0x1 << offset)) != 0)
		return true;
	else
		return false;
}

/* Returns the first free frame */
static uint32 first_free_frame(void) {
	uint32 index, offset;
	for (index = 0; index < nframes / 32; index++) {
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
		/* Find a free frame */
		uint32 index = first_free_frame();

		if (index == 0xffffffff) {
			panic("alloc_frame(): no frames available!");
		}

		/* Claim the frame */
		/* First, make sure it's currently set to being unused. */
		assert(test_frame(index) == false);
		set_frame(index);

		/* Set up the page associated with this frame */
		page->present = 1;
		page->rw = (writable ? 1 : 0);
		page->user = (kernelmode ? 0 : 1); /* we call it kernel mode, but the PTE calls it "user mode", so flip the choice */
		page->frame = index;
	}
}

/* Free a frame */
void free_frame(page_t *page) {
	if (page->frame == 0)
		return;

	/* Make sure this frame is currently set as being used, then clear it */
	assert(test_frame(page->frame) == true);
	clear_frame(page->frame);
	page->frame = 0;
	/* TODO: shouldn't we set page->present = 0 here? */
}

/* Sets up everything required and activates paging. */
void init_paging(unsigned long upper_mem) {
	uint32 mem_end_page = 0x100000 + (uint32)upper_mem*1024;
	printk("init_paging: mem_end_page = %08x (upper_mem = %u kiB)\n", mem_end_page, upper_mem);
	if (!IS_PAGE_ALIGNED(mem_end_page)) {
		/* Ignore the last few bytes of RAM to align */
		mem_end_page &= 0xfffff000;
	}

	nframes = mem_end_page / PAGE_SIZE; /* divide by page size */

	/* allocate and initialize the bitmap */
	used_frames = (uint32 *)kmalloc(nframes / 32);
	memset(used_frames, 0, nframes / 32);

	/* Create a page directory */
	kernel_directory = (page_directory_t *)kmalloc_a(sizeof(page_directory_t));
	memset(kernel_directory, 0, sizeof(page_directory_t));
	current_directory = kernel_directory;


	/* Create all the page tables... */
	/* So, this is an ugly hack. However, it may in fact be less ugly to me than to use kmalloc() in heap_expand(), 
	 * which is called by kmalloc() WHEN WE HAVE NO MEMORY LEFT IN THE HEAP! */
	assert(kernel_directory != NULL);
	for (int i = 0; i < 1024; i++) {

		uint32 phys_addr;
		/* allocate it */
		kernel_directory->tables[i] = (page_table_t *)kmalloc_ap(sizeof(page_table_t), &phys_addr);
		/* zero the new table */
		memset(kernel_directory->tables[i], 0, PAGE_SIZE);

		/* clear the low bits */
		phys_addr &= 0xfffff000;

		phys_addr |= 0x7; /* Set the present, r/w and supervisor flags (for the page table, not for the pages!) */
		kernel_directory->tables_physical[i] = phys_addr;
	}

	/*
	 * Identity map from the beginning (0x0) of memory to
	 * to the end of used memory, so that we can access it
	 * as if paging wasn't enabled.
	 */
	int i = 0;
	while (i < placement_address + PAGE_SIZE) {
		/* Kernel code is readable but not writable from userspace */
		alloc_frame(get_page(i, kernel_directory), PAGE_USER, PAGE_READONLY);
		i += PAGE_SIZE;
	}

	/* Allocate pages for the kernel heap */
	for (i = KHEAP_START; i < KHEAP_START + KHEAP_INITIAL_SIZE; i += PAGE_SIZE) {
		alloc_frame( get_page(i, kernel_directory), PAGE_USER, PAGE_READONLY);
	}

	/* Register the page fault handler */
	register_interrupt_handler(EXCEPTION_PAGE_FAULT, page_fault_handler);

	/* Enable paging! */
	switch_page_directory(kernel_directory);

	/* Initialize the kernel heap */
	kheap = create_heap(KHEAP_START, KHEAP_INITIAL_SIZE, 0xCFFFF000, 0, 0);

#if HEAP_DEBUG >= 3
	printk("init_paging() just finished; here's the current heap index\n");
	print_heap_index();
#endif
}

/* Loads the page directory at /new/ into the CR3 register. */
void switch_page_directory(page_directory_t *dir) {
	current_directory = dir;
	asm volatile("mov %0, %%cr3;" /* set the page directory register */
			     "mov %%cr0, %%eax;"
				 "or $0x80000000, %%eax;" /* PG = 1! */
				 "mov %%eax, %%cr0"
				 : /* no outputs */
				 : "r"(&dir->tables_physical)
				 : "%eax");
}

bool addr_is_mapped(uint32 addr) {
	/* TODO: this function may or may not DO WHAT IT IS CALLED.
	 * Hopefully, it does work... */
	page_t *page = get_page(addr, kernel_directory);

	return (page != NULL);
}

/* Returns a pointer to the page entry responsible for the address at /addr/. */
page_t *get_page (uint32 addr, page_directory_t *dir) {
	/* Turn the address into an index. */
	addr /= PAGE_SIZE;

	/* Find the page table containing this address */
	uint32 table_idx = addr / 1024;

	/* Check whether this address/page already has a table; if so, just return the page */
	if (dir->tables[table_idx] != NULL) {
		return & dir->tables[table_idx]->pages[addr % 1024]; /* addr%1024 works as the offset into the table */
	}
	else {
		/* Page doesn't already have a table, and creation isn't managed here any more; give up */
		return NULL;
	}
}

/* The page fault interrupt handler. */
void page_fault_handler(registers_t regs) {
	/* Whenever a page fault occurs, the CR2 register contains the fault address. */
	uint32 faulting_address;
	asm volatile("mov %%cr2, %0" : "=r" (faulting_address));

	/* We also get an error code pushed to the stack, located in regs.err_code. */
	bool present_bit = (regs.err_code & (1 << 0));   // 0 = non-present page; 1 = protection violation
	bool write_bit = regs.err_code & (1 << 1);       // was the access a write? true if write, false if read
	bool user_bit = regs.err_code & (1 << 2);        // did the access happen from user mode (ring 3) or kernel mode (ring 0)?   
	bool reserved_bit = regs.err_code & (1 << 3);    // was the fault caused by us setting a reserved bit to 1 in entry?
	bool int_fetch_bit = regs.err_code & (1 << 4);   // was the fault caused by an instruction fetch?

	/* Print a message and panic */
	printk("Page fault!\n"
		   "Flags set: %s %s %s %s %s\n"
		   "Faulting address: 0x%x\n", 
		   (present_bit ? "protection_violation" : "non_present_page"),
		   (write_bit   ? "action=write" : "action=read"),
		   (user_bit    ? "user-mode" : "kernel-mode"),
		   (reserved_bit? "reserved_bits_trampled" : ""),
		   (int_fetch_bit?"int_fetch" : ""),
		   faulting_address);

	printk("Address is %s; heap end address is %p\n", (addr_is_mapped(faulting_address)) ? "mapped" : "UNMAPPED", (kheap == 0) ? 0 : kheap->end_address);

	panic("Page fault");
}
