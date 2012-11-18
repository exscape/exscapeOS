#include <kernel/paging.h>
#include <kernel/interrupts.h>
#include <types.h>
#include <string.h> /* memset */
#include <kernel/kernutil.h> /* panic */
#include <kernel/kheap.h>
#include <kernel/console.h> /* printk */

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

uint32 mem_end_page = 0;

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
	uint32 index = INDEX_FROM_BIT(frame_addr);
	assert (index <= nframes/32 - 1);
	uint32 offset = OFFSET_FROM_BIT(frame_addr);
	used_frames[index] |= (0x1 << offset);
}

/* Clear a bit in the used_frames bitmap */
static void clear_frame(uint32 frame_addr) {
	uint32 index = INDEX_FROM_BIT(frame_addr);
	assert (index <= nframes/32 - 1);
	uint32 offset = OFFSET_FROM_BIT(frame_addr);
	used_frames[index] &= ~(0x1 << offset);
}

/* Test whether a bit is set in the used_frames bitmap */
static bool test_frame(uint32 frame_addr) {
	uint32 index = INDEX_FROM_BIT(frame_addr);
	assert (index <= nframes/32 - 1);
	uint32 offset = OFFSET_FROM_BIT(frame_addr);
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

/* Returns the amount of *physical* RAM that it still unused, i.e. unused_frame_count * 4096
 * Note that this function is simple, not fast! It should NOT be called often, e.g. in loops! */
uint32 free_bytes(void) {
	uint32 unused = 0;

	for (uint32 index = 0; index < nframes/32; index++) {
		if (used_frames[index] == 0) {
			/* All 32 frames in this bitmap chunk are free */
			unused += 4096 * 32;
			continue;
		}
		else if (used_frames[index] == 0xffffffff) {
			/* All 32 frames in this bitmap chunk are used */
			continue;
		}

		/* We're somewhere in between all used and all free; let's check a bit closer */
		for (uint32 offset = 0; offset < 32; offset++) {
			if ( (used_frames[index] & (1 << offset)) == 0 ) {
				unused += 4096;
			}
		}
	}

	return unused;
}


/**********************************
 **** END BITMAP HANDLING CODE ****
 **********************************/

// JUST MAPS, without checking if the frame is used or anything.
// Use with caution!
void map_phys_to_virt(uint32 physical_addr, uint32 virtual_addr, bool kernelmode, bool writable) {
	assert((physical_addr & 0xfff) == 0);
	assert((virtual_addr  & 0xfff) == 0);

	page_t *page = get_page(virtual_addr, true, kernel_directory); // TODO: should there be a parameter for this?

	assert(mem_end_page != 0); // This needs to be set up first!
	assert(page != NULL);

	page->present = 1;
	page->rw = (writable ? 1 : 0);
	page->user = (kernelmode ? 0 : 1);
	page->frame = (physical_addr / PAGE_SIZE);
}

void map_phys_to_virt_alloc(uint32 physical_addr, uint32 virtual_addr, bool kernelmode, bool writable) {
	assert((physical_addr & 0xfff) == 0);
	assert((virtual_addr  & 0xfff) == 0);

	page_t *page = get_page(virtual_addr, true, kernel_directory); // TODO: should there be a parameter for this?

	assert(mem_end_page != 0); // This needs to be set up first!

	if (physical_addr < mem_end_page) {
		// This is a regular memory address, as opposed to MMIO and stuff
		if (page->frame != 0) {
			/* This frame is already allocated */
			return;
		}
		else {
			uint32 index = physical_addr / 4096;

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
	else {
		assert(page != NULL);

		page->present = 1;
		page->rw = (writable ? 1 : 0);
		page->user = (kernelmode ? 0 : 1);
		page->frame = (physical_addr / PAGE_SIZE);
	}
}

static void alloc_frame_to_page(page_t *page, bool kernelmode, bool writable) {
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

/* Allocate a frame */
void alloc_frame(uint32 virtual_addr, page_directory_t *page_dir, bool kernelmode, bool writable) {
	page_t *page = get_page(virtual_addr, true, page_dir);

	alloc_frame_to_page(page, kernelmode, writable);

	/* Make sure the CPU doesn't cache the old values */
	/* TODO: there's no need to invalidate the TLB in the wrong address space, is there? Check for this! */
	invalidate_tlb((void *)virtual_addr);
}

/* Free a frame */
void free_frame(uint32 virtual_addr, page_directory_t *page_dir) {
	page_t *page = get_page(virtual_addr, true, page_dir);
	if (page->frame == 0)
		return;

	/* Make sure this frame is currently set as being used, then clear it */
	assert(test_frame(page->frame) == true);
	clear_frame(page->frame);
	page->frame = 0;
	page->present = 0;

	/* Make sure the CPU doesn't cache the old values */
	invalidate_tlb((void *)virtual_addr);
}

void init_double_fault_handler(page_directory_t *pagedir_addr);

/* Sets up everything required and activates paging. */
void init_paging(unsigned long upper_mem) {
	assert(sizeof(page_t) == 4);

	/* upper_mem is provided by GRUB; it's the number of *continuous* kilobytes of memory starting at 1MB (0x100000). */
	mem_end_page = 0x100000 + (uint32)upper_mem*1024;
	//printk("init_paging: mem_end_page = %08x (upper_mem = %u kiB)\n", mem_end_page, upper_mem);

	/* Ignore the last few bytes of RAM to align, if necessary */
	mem_end_page &= 0xfffff000;

	/* The size of the bitmap is one bit per page */
	nframes = mem_end_page / PAGE_SIZE;

	/* allocate and initialize the bitmap */
	// TODO: can nframes % 32 != 0, causing the allocation to be off-by-one?
	used_frames = (uint32 *)kmalloc(nframes / 32);
	memset(used_frames, 0, nframes / 32);

	/* Create a page directory */
	kernel_directory = (page_directory_t *)kmalloc_a(sizeof(page_directory_t));
	memset(kernel_directory, 0, sizeof(page_directory_t));
	kernel_directory->physical_address = (uint32)kernel_directory->tables_physical;

	/* Create all the page tables... */
#if 0
	assert(kernel_directory != NULL);
	for (uint32 i = 0; i < 1024; i++) {

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
#endif

	/* Create ALL the page tables that may be necessary for the kernel heap.
	 * This way, we cannot run in to the godawful situation of malloc() -> heap full -> heap_expand() -> get_page() -> malloc() new page table (while heap is full!)! */
	uint32 addr = 0;
	assert(KHEAP_START == 0xc0000000);
	assert(KHEAP_MAX_ADDR == 0xcffff000);
	for (addr = KHEAP_START; addr < KHEAP_MAX_ADDR; addr += PAGE_SIZE) {
		get_page(addr, true, kernel_directory);
	}

	/*
	 * Identity map from the beginning (0x0) of memory to
	 * to the end of used memory, so that we can access it
	 * as if paging wasn't enabled.
	 */

	addr = 0;
	while (addr < placement_address + PAGE_SIZE) {
		map_phys_to_virt_alloc(addr, addr, false, true);
		addr += PAGE_SIZE;
	}

	/* Set the page at virtual address 0 to not present (to guard against null pointer dereferences) */
	page_t *tmp_page = get_page(0, true, kernel_directory);
	tmp_page->present = 0;
	invalidate_tlb((void *)0);

	/* Allocate pages for the kernel heap. While we created page tables for the entire possible space,
	 * we obviously can't ALLOCATE 256MB for the kernel heap until it's actually required. Instead, allocate
	 * enough for the initial size. */
	for (addr = KHEAP_START; addr < KHEAP_START + KHEAP_INITIAL_SIZE; addr += PAGE_SIZE) {
		alloc_frame(addr, kernel_directory, PAGE_KERNEL, PAGE_WRITABLE);
	}

	/* Register the page fault handler */
	register_interrupt_handler(EXCEPTION_PAGE_FAULT, page_fault_handler);

	/* Enable paging! */
	switch_page_directory(kernel_directory);

	/* Initialize the kernel heap */
	kheap = create_heap(KHEAP_START, KHEAP_INITIAL_SIZE, KHEAP_MAX_ADDR, 1, 0); /* supervisor, not read-only */

	/* Set up the current page directory */
	//current_directory = clone_directory(kernel_directory);
	current_directory = kernel_directory;
	switch_page_directory(current_directory);

#if HEAP_DEBUG >= 3
	printk("init_paging() just finished; here's the current heap index\n");
	print_heap_index();
#endif
	init_double_fault_handler(kernel_directory);
}

extern struct idt_entry idt[256];
bool addr_is_mapped_in_dir(uint32 addr, page_directory_t *dir);

/* Loads the page directory at /dir/ into the CR3 register. */
void switch_page_directory(page_directory_t *dir) {
	current_directory = dir;
	uint32 new_cr3_contents = (uint32) dir->physical_address;
	/* bit 3 and 4 (i.e. with values 8 and 16) are used to control write-through and cache, but we don't want either set.
	 * the rest of the low bits are ignored, according to Intel docs. Still, I prefer them to be 0, just in case. */
	assert((new_cr3_contents & 0xfff) == 0);

	asm volatile("mov %0, %%cr3;" /* set the page directory register */
			     "mov %%cr0, %%eax;"
				 "or $0x80000000, %%eax;" /* PG = 1! */
				 "mov %%eax, %%cr0"
				 : /* no outputs */
				 : "r"(new_cr3_contents)
				 : "%eax");
}

bool addr_is_mapped(uint32 addr) {
	page_t *page = get_page(addr, /*create = */ false, current_directory);
	if (page == NULL)
		return false;

	return (page->present == 1 && page->frame != 0);
}

bool addr_is_mapped_in_dir(uint32 addr, page_directory_t *dir) {
	page_t *page = get_page(addr, /*create = */ false, dir);
	if (page == NULL)
		return false;

	return (page->present == 1 && page->frame != 0);
}

/* Returns a pointer to the page entry responsible for the address at /addr/. */
page_t *get_page (uint32 addr, bool create, page_directory_t *dir) {
	/* Turn the address into an index. */
	addr /= PAGE_SIZE;

	/* Find the page table containing this address */
	uint32 table_idx = addr / 1024;

	/* Check whether this address/page already has a table; if so, just return the page */
	if (dir->tables[table_idx] != NULL) {
		return & dir->tables[table_idx]->pages[addr % 1024]; /* addr%1024 works as the offset into the table */
	}
	else if (create == true) {
		uint32 phys;
		dir->tables[table_idx] = (page_table_t *)kmalloc_ap(sizeof(page_table_t), &phys);
		memset(dir->tables[table_idx], 0, sizeof(page_table_t));
		dir->tables_physical[table_idx] = phys | 0x07;
		return & dir->tables[table_idx]->pages[addr % 1024]; /* addr%1024 works as the offset into the table */
	}
	else {
		/* Page doesn't already have a table, and creation isn't managed here any more; give up */
		return NULL;
	}
}

uint32 virtual_to_physical(uint32 virt_addr, page_directory_t *page_dir) {
	/* Converts a virtual address (in the current address space) to a physical address, if possible. */

	assert(virt_addr >= 0x1000); /* addresses below 0x1000 are unmapped, and nobody should ask for them */

	page_t *page = get_page(virt_addr, false, page_dir);
	if (page == NULL) {
		panic("virtual_to_physical on non-created page");
	}

	if (page->present == 0 || page->frame == 0) {
		/* Frame 0 is probably never used in protected mode; nevertheless, this is a minor bug (IF frame 0 is valid;
		 * from memory, I do believe they're 0-indexed) */
		panic("virtual_to_physical: page not present");
	}

	/* calculate the address for the beginning of the frame */
	uint32 phys_addr = page->frame * 0x1000;
	/* add the offset within the page */
	phys_addr += (virt_addr & 0xfff);

	return phys_addr;
}

void copy_page_physical (uint32 src, uint32 dest);

page_directory_t *create_user_page_dir(void) {
	uint32 new_dir_phys;
	page_directory_t *dir = kmalloc_ap(sizeof(page_directory_t), &new_dir_phys);

	disable_interrupts();

	/* Since we want the kernel mapping to be the same in all address spaces, and the kernel (+ kernel heap, etc.) is
	 * all that exists in the kernel directory, copy it! */
	memcpy(dir, kernel_directory, sizeof(page_directory_t));

	enable_interrupts();

	/*
	 * We need the physical address of the /tables_physical/ struct member. /dir/ points to the beginning of the structure, of course.
	 * Since the physical address is (obviously!) in another address space, we can't use the & operator, but must instead calculate
	 * its offset into the structure, then add that to the physical address.
	 */
	uint32 offset = (uint32)dir->tables_physical - (uint32)dir;
	dir->physical_address = new_dir_phys + offset;

	return dir;
}

#if 0
static page_table_t *clone_table(page_table_t *src, uint32 *physaddr) {
	/* Create a new, empty page table */
	page_table_t *table = (page_table_t *)kmalloc_ap(sizeof(page_table_t), physaddr);
	memset(table, 0, sizeof(page_table_t));

	/* Copy entries */
	for (uint32 i = 0; i < 1024; i++) {
		if (src->pages[i].frame == 0)
			continue;

		/* Allocate a new frame to hold the data (since we must copy it) */
		alloc_frame_to_page(&table->pages[i], 0, 0); /* TODO: #defines instead of zeroes */

		/* TODO: can't this be done in a prettier way? */
		if (src->pages[i].present) table->pages[i].present = 1;
		if (src->pages[i].rw)      table->pages[i].rw = 1;
		if (src->pages[i].user)    table->pages[i].user = 1;
		if (src->pages[i].pwt)    table->pages[i].pwt = 1;
		if (src->pages[i].pcd)    table->pages[i].pcd = 1;
		if (src->pages[i].accessed) table->pages[i].accessed = 1;
		if (src->pages[i].dirty)   table->pages[i].dirty = 1;
		if (src->pages[i].pat)   table->pages[i].pat = 1;
		if (src->pages[i].global)   table->pages[i].global = 1;
		if (src->pages[i].avail)   table->pages[i].avail = src->pages[i].avail;

		/* Copy the data between the frames */
		copy_page_physical(src->pages[i].frame * PAGE_SIZE, table->pages[i].frame * PAGE_SIZE);
	}

	return table;
}

page_directory_t *clone_directory(page_directory_t *src) {
	/* Create a new, empty page directory */
	uint32 new_dir_phys;
	page_directory_t *dir = (page_directory_t *)kmalloc_ap(sizeof(page_directory_t), &new_dir_phys);
	memset(dir, 0, sizeof(page_directory_t));

	/*
	 * We need the physical address of the /physical_address/ struct member. /dir/ points to the beginning of the structure, of course.
	 * Since the physical address is (obviously!) in another address space, we can't use the & operator, but must instead calculate
	 * its offset into the structure, then add that to the physical address.
	 */
	uint32 offset = (uint32)dir->tables_physical - (uint32)dir;
	dir->physical_address = new_dir_phys + offset;

	/* Clone the page tables */
	for (uint32 i = 0; i < 1024; i++) {
		if (src->tables[i] == NULL) {
			continue;
		}

		/*
		 * TODO: I have my doubts about this procedure... Will the kernel directory stay in sync across all address spaces if it changes?
		 */

		if (kernel_directory->tables[i] == src->tables[i]) {
			/* This table is in the kernel; just link it (don't clone it) */
			dir->tables[i] = src->tables[i];
			dir->tables_physical[i] = src->tables_physical[i];
		}
		else {
			/* Copy the table */
			uint32 phys;
			dir->tables[i] = clone_table(src->tables[i], &phys);
			dir->tables_physical[i] = phys | 0x07; /* Present, RW, User */
		}
	}

	flush_all_tlb(); /* TODO: is this the right place to do this? */

	return dir;
}
#endif

/* Tells the CPU that the page at this (virtual) address has changed. */
void invalidate_tlb(void *addr) {
	   asm volatile("invlpg (%%eax)" : : "a" (addr) );
}

void flush_all_tlb(void) {
	asm volatile ("push %eax; mov %cr3, %eax; mov %eax, %cr3; pop %eax;");
}

/* The page fault interrupt handler. */
uint32 page_fault_handler(uint32 esp) {
	registers_t *regs = (registers_t *)esp;
	/* Whenever a page fault occurs, the CR2 register contains the fault address. */
	uint32 faulting_address;
	asm volatile("mov %%cr2, %0" : "=r" (faulting_address));

	/* We also get an error code pushed to the stack, located in regs->err_code. */
	bool present_bit = (regs->err_code & (1 << 0));   // 0 = non-present page; 1 = protection violation
	bool write_bit = regs->err_code & (1 << 1);       // was the access a write? true if write, false if read
	bool user_bit = regs->err_code & (1 << 2);        // did the access happen from user mode (ring 3) or kernel mode (ring 0)?   
	bool reserved_bit = regs->err_code & (1 << 3);    // was the fault caused by us setting a reserved bit to 1 in entry?
	bool int_fetch_bit = regs->err_code & (1 << 4);   // was the fault caused by an instruction fetch?

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

	return esp; /* not reached at the moment */
}
