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

list_t *pagedirs = NULL;

static void _vmm_invalidate(void *addr);
static void _vmm_map(uint32 virtual, uint32 physical, page_directory_t *dir, bool kernelmode, bool writable);
static page_t *_vmm_get_page(uint32 virtual, page_directory_t *dir, bool kernelspace);
static void _vmm_create_page_table(uint32 pt_index, page_directory_t *dir, bool kernelspace);
//static void _vmm_flush_tlb(void);

/* Bitmap macros */
/* 32 == sizeof(uint32) in bits, so these simply calculate which dword a bit belongs to,
 * and the number of bits to shift that dword to find it, respectively. */
#define INDEX_FROM_BIT(a) (a / 32)
#define OFFSET_FROM_BIT(a) (a % 32)

/******************************
 **** BITMAP HANDLING CODE ****
 ******************************/

/* Set a bit in the used_frames bitmap */
static void _pmm_set_frame(uint32 phys_addr) {
	phys_addr /= PAGE_SIZE;
	uint32 index = INDEX_FROM_BIT(phys_addr);
	assert (index <= nframes/32 - 1);
	uint32 offset = OFFSET_FROM_BIT(phys_addr);
	assert((used_frames[index] & (1 << offset)) == 0);
	used_frames[index] |= (1 << offset);
}

/* Clear a bit in the used_frames bitmap */
static void _pmm_clear_frame(uint32 phys_addr) {
	phys_addr /= PAGE_SIZE;
	uint32 index = INDEX_FROM_BIT(phys_addr);
	assert (index <= nframes/32 - 1);
	uint32 offset = OFFSET_FROM_BIT(phys_addr);
	assert((used_frames[index] & (1 << offset)) != 0);
	used_frames[index] &= ~(1 << offset);
}

/* Test whether a bit is set in the used_frames bitmap */
static bool _pmm_test_frame(uint32 phys_addr) {
	phys_addr /= PAGE_SIZE;
	uint32 index = INDEX_FROM_BIT(phys_addr);
	assert (index <= nframes/32 - 1);
	uint32 offset = OFFSET_FROM_BIT(phys_addr);
	if ((used_frames[index] & (1 << offset)) != 0)
		return true;
	else
		return false;
}

/* Returns the first free frame, roughly after (or at) /start_addr/ */
static uint32 _pmm_first_free_frame(uint32 start_addr) {
	uint32 index = start_addr / PAGE_SIZE;
	if (index != 0)
		index -= 1; // TODO: fix this - this is to be on the safe side by wasting time instead of getting bad results during the initial implementation phase
	for (; index < nframes / 32; index++) {
		if (used_frames[index] == 0xffffffff) {
			/* No bits are free among the 32 tested; try the next index */
			continue;
		}

		/* Since we're still here, at least one bit among these 32 is zero... Let's find the first. */
		// Offset starts at 0 which means we *may* return something earlier than start_addr,
		// but that is only indended as a rough guide, not a hard rule.
		for (uint32 offset = 0; offset < 32; offset++) {
			if ((used_frames[index] & (1 << offset)) == 0) {
				/* Found it! Return the frame address. */
				return (index * 32 + offset) * PAGE_SIZE;
			}
		}
	}

	/* If this is reached, there were no free frames! */
	return 0xffffffff;
}

uint32 pmm_alloc(void) {
	INTERRUPT_LOCK;
	// TODO: keep track of the first free frame for quicker access
	uint32 phys_addr = _pmm_first_free_frame(0);
	if (phys_addr == 0xffffffff) {
		panic("pmm_alloc: no free frames (out of memory)!");
	}
	_pmm_set_frame(phys_addr); // also tests that it's actually free
	INTERRUPT_UNLOCK;
	return phys_addr;
}

// Allocates /num_frames/ continuous physical frames
uint32 pmm_alloc_continuous(uint32 num_frames) {
	if (num_frames < 2)
		return pmm_alloc();

	panic("TODO: pmm_alloc_continuous - test it!");

	uint32 last = placement_address + PAGE_SIZE; // don't bother trying prior to this
	uint32 start = _pmm_first_free_frame(last);
	bool success = false;

	INTERRUPT_LOCK;

	while (!success) {
		success = true; // if set when the for loop breaks, we're done
		if (start + (num_frames - 1) * PAGE_SIZE > mem_end_page)
			panic("pmm_alloc_continuous: no large enough continuous region found");

		for (uint32 i=1; i < num_frames; i++) { // we know that start + 0 is free, so start looking at 1
			if (_pmm_test_frame(start + (i * PAGE_SIZE)) != 0) {
				// We found a non-free frame! D'oh!
				// Start over at the next possibly free address.
				last = start + ((i+1) * PAGE_SIZE);
				success = false;
				break;
			}
		}
		// if the for loop didn't break because of finding a page, success == true and we'll exit
	}

	// Phew! /num_frames/ starting at (and including) /start/ ought to be free now.
	for(uint32 i=0; i < num_frames; i++) {
		_pmm_set_frame(start + i * PAGE_SIZE);
	}

	INTERRUPT_UNLOCK;

	return start;
}

void pmm_free(uint32 phys_addr) {
	INTERRUPT_LOCK;
	_pmm_clear_frame(phys_addr);
	INTERRUPT_UNLOCK;
}

/* Returns the amount of *physical* RAM that it still unused, i.e. unused_frame_count * 4096
 * Note that this function is simple, not fast! It should NOT be called often, e.g. in loops! */
uint32 pmm_bytes_free(void) {
	uint32 unused = 0;

	for (uint32 index = 0; index < nframes/32; index++) {
		if (used_frames[index] == 0) {
			/* All 32 frames in this bitmap chunk are free */
			unused += PAGE_SIZE * 32;
			continue;
		}
		else if (used_frames[index] == 0xffffffff) {
			/* All 32 frames in this bitmap chunk are used */
			continue;
		}

		/* We're somewhere in between all used and all free; let's check a bit closer */
		for (uint32 offset = 0; offset < 32; offset++) {
			if ( (used_frames[index] & (1 << offset)) == 0 ) {
				unused += PAGE_SIZE;
			}
		}
	}

	return unused;
}

uint32 vmm_alloc_kernel(uint32 start_virtual, uint32 end_virtual, bool continuous_physical, bool writable) {
	INTERRUPT_LOCK;
	assert(end_virtual > start_virtual);
	assert((start_virtual & 0xfff) == 0);
	assert((end_virtual & 0xfff) == 0);
	assert(((end_virtual - start_virtual) & 0xfff) == 0);
	assert(continuous_physical == !!continuous_physical);
	assert(writable == !!writable);

	if (continuous_physical) {
		const uint32 ret = pmm_alloc_continuous(end_virtual - start_virtual /* size */);
		uint32 phys = ret;
		for (uint32 addr = start_virtual; addr < end_virtual; addr += PAGE_SIZE, phys += PAGE_SIZE) {
			vmm_map_kernel(addr, phys, writable);
		}
		INTERRUPT_UNLOCK;
		return ret;
	}
	else {
		// Non-continuous
		uint32 ret = 0;
		for (uint32 addr = start_virtual; addr < end_virtual; addr += PAGE_SIZE) {	
			uint32 phys = pmm_alloc();
			if (ret == 0)
				ret = phys; // store the first physical address

			vmm_map_kernel(addr, phys, writable);
		}
		INTERRUPT_UNLOCK;
		return ret;
	}
}

void vmm_alloc_user(uint32 start_virtual, uint32 end_virtual, page_directory_t *dir, bool writable) {
	INTERRUPT_LOCK;
	assert(end_virtual > start_virtual);
	assert((start_virtual & 0xfff) == 0);
	assert((end_virtual & 0xfff) == 0);
	assert(((end_virtual - start_virtual) & 0xfff) == 0);
	assert(writable == !!writable);

		for (uint32 addr = start_virtual; addr < end_virtual; addr += PAGE_SIZE) {
			uint32 phys = pmm_alloc();
			_vmm_map(addr, phys, dir, false /* user mode */, writable);
		}

	INTERRUPT_UNLOCK;
}

void vmm_unmap(uint32 virtual, page_directory_t *dir) {
	INTERRUPT_LOCK;
	assert(dir != NULL);
	page_t *page = _vmm_get_page(virtual, dir, (dir == kernel_directory ? true : false));
	assert(page != NULL);
	*((uint32 *)page) = 0;

	_vmm_invalidate((void *)virtual);
	INTERRUPT_UNLOCK;
}

void vmm_free(uint32 virtual, page_directory_t *dir) {
	INTERRUPT_LOCK;
	assert(dir != NULL);
	pmm_free(vmm_get_phys(virtual, dir));
	vmm_unmap(virtual, dir);
	INTERRUPT_UNLOCK;
}

uint32 vmm_get_phys(uint32 virtual, page_directory_t *dir) {
	INTERRUPT_LOCK;
	assert(dir != NULL);
	page_t *page = _vmm_get_page(virtual, dir, (dir == kernel_directory ? true : false));
	assert(page != 0);
	uint32 phys = page->frame * PAGE_SIZE;
	phys += (virtual & 0xfff); // add the offset into the page

	INTERRUPT_UNLOCK;
	return phys;
}

void vmm_set_guard(uint32 virtual, page_directory_t *dir, bool guard /* true to set, false to clear */) {
	INTERRUPT_LOCK;
	assert(dir != NULL);
	assert(guard == !!guard);

	page_t *page = _vmm_get_page(virtual, dir, (dir == kernel_directory ? true : false));
	assert(page->present == guard); // if guard is true: should be true (before), else false
	assert(page->guard == !guard);  // if guard is true: should be false (before), else true
	page->present = !guard;
	page->guard = guard; // custom bit from the "avail" bits

	_vmm_invalidate((void *)virtual);
	INTERRUPT_UNLOCK;
}

void vmm_map_kernel(uint32 virtual, uint32 physical, bool writable) {
	_vmm_map(virtual, physical, kernel_directory, true /* kernel mode */, writable);
}

static void _vmm_map(uint32 virtual, uint32 physical, page_directory_t *dir, bool kernelmode, bool writable) {
	INTERRUPT_LOCK;
	assert(dir != NULL);
	assert(kernelmode == !!kernelmode);
	assert(writable == !!writable);

	page_t *page = _vmm_get_page(virtual, dir, kernelmode);
	assert(*((uint32 *)page) == 0);

	if (physical <= placement_address) {
		printk(".\b");
	}
	page->frame = (physical / PAGE_SIZE);
	page->user = !kernelmode;
	page->rw = !!writable;
	page->present = 1;

	_vmm_invalidate((void *)virtual);
	INTERRUPT_UNLOCK;
}

static page_t *_vmm_get_page(uint32 virtual, page_directory_t *dir, bool kernelspace) {
	INTERRUPT_LOCK;
	assert(dir != NULL);
	assert(kernelspace == !!kernelspace);

	uint32 pt_index = virtual / 4096 / 1024;
	uint32 pt_offset = (virtual / 4096) % 1024;
	page_table_t *table = dir->tables[pt_index];

	if (table == NULL) {
		_vmm_create_page_table(pt_index, dir, kernelspace);
	}

	INTERRUPT_UNLOCK;

	return &dir->tables[pt_index]->pages[pt_offset];
}

#include <stdio.h> // TODO: remove when temporary panic() call is gone
static void _vmm_create_page_table(uint32 pt_index, page_directory_t *dir, bool kernelspace) {
	INTERRUPT_LOCK;

	assert(dir->tables[pt_index] == NULL);
	assert(kernelspace == !!kernelspace);

	uint32 phys;
	dir->tables[pt_index] = (page_table_t *)kmalloc_ap(sizeof(page_table_t), &phys);
	memset(dir->tables[pt_index], 0, sizeof(page_table_t));
	dir->tables_physical[pt_index] = phys | PAGE_PRESENT | PAGE_USER | PAGE_RW; // TODO: access bits are currently used on a page-level only

	if (kernelspace) {
		assert(dir == kernel_directory);
		  // This belongs to kernel space, and needs to be in sync across
			// *ALL* user mode tasks as well
			for (node_t *it = pagedirs->head; it != NULL; it = it->next) {
				page_directory_t *d = (page_directory_t *)it->data;
				if (d != kernel_directory && d != 0 && d != dir) {
					d->tables[pt_index] = dir->tables[pt_index];
					d->tables_physical[pt_index] = dir->tables_physical[pt_index];

					char buf[128] = {0};
					sprintf(buf, "updated task's page dir (dir 0x%08x); page table = 0x%08x - TODO: change to printk\n", d, dir->tables);
					panic(buf);
				}
			}
	}
	else
		assert(dir != kernel_directory);

	INTERRUPT_UNLOCK;
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
	used_frames = (uint32 *)kmalloc((nframes / 32 + 1) * sizeof(uint32));
	memset(used_frames, 0, (nframes / 32 + 1) * sizeof(uint32));

	pagedirs = list_create();

	/* Create a page directory */
	kernel_directory = (page_directory_t *)kmalloc_a(sizeof(page_directory_t));
	memset(kernel_directory, 0, sizeof(page_directory_t));
	kernel_directory->physical_address = (uint32)kernel_directory->tables_physical;

	list_append(pagedirs, kernel_directory); // TODO: should this be the one left out of the list?

	/* Create ALL the page tables that may be necessary for the kernel heap.
	 * This way, we cannot run in to the godawful situation of malloc() -> heap full -> heap_expand() -> get_page() -> malloc() new page table (while heap is full!)! */
	for (uint32 index = (KHEAP_START / PAGE_SIZE / 1024); index <= (KHEAP_MAX_ADDR / PAGE_SIZE / 1024); index++) {
		_vmm_create_page_table(index, kernel_directory, true /* kernelspace */);
	}

	/*
	 * Identity map from the beginning (0x0) of memory to
	 * to the end of used memory, so that we can access it
	 * as if paging wasn't enabled.
	 */
	uint32 addr = 0;
	while (addr < placement_address + PAGE_SIZE) {
		vmm_map_kernel(addr, addr, true); // TODO: parts should be read only
		_pmm_set_frame(addr);
		addr += PAGE_SIZE;
	}

	// Set address 0 as a guard page, to catch NULL pointer dereferences
	vmm_set_guard(0 /* address */, kernel_directory, true);

	/* Allocate pages for the kernel heap. While we created page tables for the entire possible space,
	 * we obviously can't ALLOCATE 256MB for the kernel heap until it's actually required. Instead, allocate
	 * enough for the initial size. */
	vmm_alloc_kernel(KHEAP_START, KHEAP_START + KHEAP_INITIAL_SIZE + PAGE_SIZE, false /* continuous physical */, true /* writable */);

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

extern volatile list_t ready_queue;

page_directory_t *create_user_page_dir(void) {
	uint32 new_dir_phys;
	page_directory_t *dir = kmalloc_ap(sizeof(page_directory_t), &new_dir_phys);

	INTERRUPT_LOCK;

	/* Since we want the kernel mapping to be the same in all address spaces, and the kernel (+ kernel heap, etc.) is
	 * all that exists in the kernel directory, copy it! */
	memcpy(dir, kernel_directory, sizeof(page_directory_t));

	list_append(pagedirs, dir);

	INTERRUPT_UNLOCK;

	/*
	 * We need the physical address of the /tables_physical/ struct member. /dir/ points to the beginning of the structure, of course.
	 * Since the physical address is (obviously!) in another address space, we can't use the & operator, but must instead calculate
	 * its offset into the structure, then add that to the physical address.
	 */
	uint32 offset = (uint32)dir->tables_physical - (uint32)dir;
	dir->physical_address = new_dir_phys + offset;

	return dir;
}

void destroy_user_page_dir(page_directory_t *dir) {
	assert(dir != NULL);
	assert(dir != kernel_directory);

	INTERRUPT_LOCK;
	for (int i=0; i < 1024; i++) {
		page_table_t *table = dir->tables[i];
		if (table == NULL)
			continue;
		else if (table == kernel_directory->tables[i]) {
			// Kernel space, let it be
			continue;
		}
		else {
			// User mode table! This should be destroyed.
			kfree(table);
		}
	}

	kfree(dir);

	INTERRUPT_UNLOCK;
}

/* Tells the CPU that the page at this (virtual) address has changed. */
static void _vmm_invalidate(void *addr) {
	asm volatile("invlpg (%%eax)" : : "a" (addr) );
}

//static void _vmm_flush_tlb(void) {
//asm volatile ("push %eax; mov %cr3, %eax; mov %eax, %cr3; pop %eax;");
//}

/* The page fault interrupt handler. */
uint32 page_fault_handler(uint32 esp) {
	registers_t *regs = (registers_t *)esp;
	/* Whenever a page fault occurs, the CR2 register contains the fault address. */
	uint32 faulting_address;
	asm volatile("mov %%cr2, %0" : "=r" (faulting_address));

	/* We also get an error code pushed to the stack, located in regs->err_code. */
	bool present_bit = (regs->err_code & (1 << 0));   // 0 = non-present page; 1 = protection violation
	bool write_bit = regs->err_code & (1 << 1);	   // was the access a write? true if write, false if read
	bool user_bit = regs->err_code & (1 << 2);		// did the access happen from user mode (ring 3) or kernel mode (ring 0)?   
	bool reserved_bit = regs->err_code & (1 << 3);	// was the fault caused by us setting a reserved bit to 1 in entry?
	bool int_fetch_bit = regs->err_code & (1 << 4);   // was the fault caused by an instruction fetch?

	/* Print a message and panic */
	printk("Page fault!\n"
		   "Flags set: %s %s %s %s %s\n"
		   "Faulting address: 0x%x\n", 
		   (present_bit ? "protection_violation" : "non_present_page"),
		   (write_bit   ? "action=write" : "action=read"),
		   (user_bit	? "user-mode" : "kernel-mode"),
		   (reserved_bit? "reserved_bits_trampled" : ""),
		   (int_fetch_bit?"int_fetch" : ""),
		   faulting_address);

	printk("Heap end address is %p\n", kheap->end_address);

	panic("Page fault");

	return esp; /* not reached at the moment */
}
