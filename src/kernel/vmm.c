#include <kernel/vmm.h>
#include <kernel/interrupts.h>
#include <types.h>
#include <string.h> /* memset */
#include <kernel/kernutil.h> /* panic */
#include <kernel/kheap.h>
#include <kernel/console.h> /* printk */
#include <kernel/pmm.h>

// The kernel's page directory
page_directory_t *kernel_directory = 0;
// The current page directory;
page_directory_t *current_directory = 0;

/* defined in kheap.c */
extern uint32 placement_address;
extern heap_t *kheap;

uint32 mem_end_page = 0;

list_t *pagedirs = NULL;

// Everything outside of the user mode address range of 0x1000000 to 0xc0000000 (exclusive) is kernel space.
// Currently, 0x10000000 is where ELF programs are loaded, while 0xbffff000 is the start of the userspace stack.
// The rest of the space (virtually that entire space except the few pages used by the program + data and
// the stack) is currently unused.
// (keep in mind that the stack grows towards LOWER addresses).
#define IS_USER_SPACE(addr) ( (addr >= 0x10000000 && addr < 0xc0000000) )
#define IS_KERNEL_SPACE(addr) ( !IS_USER_SPACE(addr) )

// Forward declarations; the internal (static, _ prefixed) functions are below the public ones
static void _vmm_invalidate(void *addr);
static void _vmm_map(uint32 virtual, uint32 physical, page_directory_t *dir, bool kernelmode, bool writable);
static page_t *_vmm_get_page(uint32 virtual, page_directory_t *dir);
static void _vmm_create_page_table(uint32 pt_index, page_directory_t *dir);
//static void _vmm_flush_tlb(void);

// Allocate physical memory for kernel space, possibly with continuous physical addresses, and map it to the selected virtual address range
// Returns the first physical address in the range (even if the range may be discontinuous).
uint32 vmm_alloc_kernel(uint32 start_virtual, uint32 end_virtual, bool continuous_physical, bool writable) {
	INTERRUPT_LOCK;
	assert(end_virtual > start_virtual);
	assert((start_virtual & 0xfff) == 0);
	assert((end_virtual & 0xfff) == 0);
	assert(((end_virtual - start_virtual) & 0xfff) == 0); // Should follow from the above, but eh
	assert(continuous_physical == !!continuous_physical);
	assert(writable == !!writable);

	if (continuous_physical) {
		const uint32 ret = pmm_alloc_continuous((end_virtual - start_virtual) / PAGE_SIZE /* size */);
		uint32 phys = ret;
		for (uint32 addr = start_virtual; addr < end_virtual; addr += PAGE_SIZE, phys += PAGE_SIZE) {
			vmm_map_kernel(addr, phys, writable);
		}
		INTERRUPT_UNLOCK;
		return ret;
	}
	else {
		// Addresses don't have to be continuous, so we can just use the regular physical allocator
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

// Allocate physical memory for user space and map it to the selected virtual address range
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

// Unmap a virtual address. Does NOT free the associated physical memory (see vmm_free for that)
void vmm_unmap(uint32 virtual, page_directory_t *dir) {
	INTERRUPT_LOCK;
	assert(dir != NULL);
	page_t *page = _vmm_get_page(virtual, dir);
	assert(page != NULL);
	*((uint32 *)page) = 0;

	_vmm_invalidate((void *)virtual);
	INTERRUPT_UNLOCK;
}

// Unmap a virtual address, and free its associated physical memory
void vmm_free(uint32 virtual, page_directory_t *dir) {
	INTERRUPT_LOCK;
	assert(dir != NULL);
	pmm_free(vmm_get_phys(virtual, dir));
	vmm_unmap(virtual, dir);
	INTERRUPT_UNLOCK;
}

// Calculate and return the physical address for a given virtual one
uint32 vmm_get_phys(uint32 virtual, page_directory_t *dir) {
	INTERRUPT_LOCK;
	assert(dir != NULL);
	page_t *page = _vmm_get_page(virtual, dir);
	assert(page != 0);
	uint32 phys = page->frame * PAGE_SIZE;
	phys += (virtual & 0xfff); // add the offset into the page

	INTERRUPT_UNLOCK;
	return phys;
}

// Set a guard page, i.e. set present to 0, to catch invalid reads/writes
void vmm_set_guard(uint32 virtual, page_directory_t *dir, bool guard /* true to set, false to clear */) {
	INTERRUPT_LOCK;
	assert(dir != NULL);
	assert(guard == !!guard);

	page_t *page = _vmm_get_page(virtual, dir);
	assert(page->present == guard); // if guard is true: should be true (before), else false
	assert(page->guard == !guard);  // if guard is true: should be false (before), else true
	page->present = !guard;
	page->guard = guard; // custom bit from the "avail" bits

	_vmm_invalidate((void *)virtual);
	INTERRUPT_UNLOCK;
}

// Map a virtual address to a physical one, in kernel space, e.g. for MMIO
void vmm_map_kernel(uint32 virtual, uint32 physical, bool writable) {
	_vmm_map(virtual, physical, kernel_directory, true /* kernel mode */, writable);
}

// Internal function: map a virtual address to a physical one, for kernel- or userspace
static void _vmm_map(uint32 virtual, uint32 physical, page_directory_t *dir, bool kernelmode, bool writable) {
	INTERRUPT_LOCK;
	assert(dir != NULL);
	assert(kernelmode == !!kernelmode);
	assert(writable == !!writable);

	page_t *page = _vmm_get_page(virtual, dir);
	assert(*((uint32 *)page) == 0);

	page->frame = (physical / PAGE_SIZE);
	page->user = !kernelmode;
	page->rw = !!writable;
	page->present = 1;

	_vmm_invalidate((void *)virtual);
	INTERRUPT_UNLOCK;
}

// Internal function: get a pointer to a page entry; create the page table containing it if necessary
static page_t *_vmm_get_page(uint32 virtual, page_directory_t *dir) {
	INTERRUPT_LOCK;
	assert(dir != NULL);

	// 4096: page size; 1024: number of pages per page table
	uint32 pt_index = virtual / 4096 / 1024;
	uint32 pt_offset = (virtual / 4096) % 1024;
	page_table_t *table = dir->tables[pt_index];

	if (table == NULL) {
		_vmm_create_page_table(pt_index, dir);
	}

	INTERRUPT_UNLOCK;

	return &dir->tables[pt_index]->pages[pt_offset];
}

#include <stdio.h> // TODO: remove when temporary panic() call is gone
// This might stay for a while, simply because the condition to test cannot happen right now;
// all kernel space page tables are created prior to user mode tasks.

// Internal function: create and set up a page table
static void _vmm_create_page_table(uint32 pt_index, page_directory_t *dir) {
	INTERRUPT_LOCK;

	assert(dir->tables[pt_index] == NULL);

	uint32 phys;
	dir->tables[pt_index] = (page_table_t *)kmalloc_ap(sizeof(page_table_t), &phys);
	memset(dir->tables[pt_index], 0, sizeof(page_table_t));
	dir->tables_physical[pt_index] = phys | PAGE_PRESENT | PAGE_USER | PAGE_RW; // TODO: access bits are currently used on a page-level only

	if (IS_KERNEL_SPACE(pt_index * 4096 * 1024)) {
		assert(dir == kernel_directory);
		  // This belongs to kernel space, and needs to be in sync across
			// *ALL* user mode tasks as well
			for (node_t *it = pagedirs->head; it != NULL; it = it->next) {
				page_directory_t *d = (page_directory_t *)it->data;
				if (d != kernel_directory && d != 0 && d != dir) {
					d->tables[pt_index] = dir->tables[pt_index];
					d->tables_physical[pt_index] = dir->tables_physical[pt_index];

					char buf[128] = {0};
					sprintf(buf, "updated task's page dir (dir 0x%08x); page table = 0x%08x - TODO: test this!\n", d, dir->tables);
					panic(buf);
				}
			}
	}
	//else // TODO: this can be enabled only when the kernel space is set to kernel mode in the paging structures!
	//assert(dir != kernel_directory);

	INTERRUPT_UNLOCK;
}

void init_double_fault_handler(page_directory_t *pagedir_addr);

extern uint32 __start_text;
extern uint32 __end_text;

extern uint32 mem_end_page;

/* Sets up everything required and activates paging. */
void init_paging(unsigned long upper_mem) {
	assert(sizeof(page_t) == 4);

	pmm_init(upper_mem);

	pagedirs = list_create();

	/* Create a page directory */
	kernel_directory = (page_directory_t *)kmalloc_a(sizeof(page_directory_t));
	memset(kernel_directory, 0, sizeof(page_directory_t));
	kernel_directory->physical_address = (uint32)kernel_directory->tables_physical;

	list_append(pagedirs, kernel_directory); // TODO: should this be the one left out of the list?

	/* Create ALL the page tables that may be necessary for the kernel heap.
	 * This way, we cannot run in to the godawful situation of malloc() -> heap full -> heap_expand() -> get_page() -> malloc() new page table (while heap is full!)! */
	for (uint32 index = (KHEAP_START / PAGE_SIZE / 1024); index <= (KHEAP_MAX_ADDR / PAGE_SIZE / 1024); index++) {
		_vmm_create_page_table(index, kernel_directory);
	}

	/*
	 * Identity map from the beginning (0x0) of memory to
	 * to the end of used memory, so that we can access it
	 * as if paging wasn't enabled.
	 */

	// We currently need the kernel's .text to be readable to user mode as well...
	// Tasks created in-kernel (not via ELF files) run code here.
	// This reads the start and end addresses of .text from the linker script,
	// maps it to page-aligned addresses and maps them differently from the rest
	// of the kernel space.
	uint32 start_text = (uint32)(&__start_text);
	uint32 end_text = (uint32)(&__end_text);
	if ((start_text & 0xfff) != 0) {
		start_text &= 0xfffff000;
		// DON'T add 0x1000 here, we want to truncate to map it all (.text starts at 0x10000c atm)
	}
	if ((end_text & 0xfff) != 0) {
		end_text &= 0xfffff000;
		end_text += 0x1000;
	}

	// Allocate the physical addresses - this should ALWAYS be the FIRST call to
	// *ANY* pmm_ function, so whe should ALWAYS get 0 back as the first address.
	uint32 first_addr = pmm_alloc_continuous((placement_address / PAGE_SIZE) + 2);
	assert(first_addr == 0);

	// Map the virtual addresses, with their respective permissions
	uint32 addr = 0;
	while (addr < placement_address + PAGE_SIZE) {
		// TODO: change this to map kernel pages as kernel mode; read-only for .text and read-write for the rest, when user tasks are no longer started in-kernel!
		if (addr >= start_text && addr < end_text) {
			// This is a kernel .text page - map it as user mode, read-only
			_vmm_map(addr, addr, kernel_directory, false, false); // user mode!
		}
		else {
			// Kernel data - mark it as user mode, read-write, for now - VERY bad. TODO
			_vmm_map(addr, addr, kernel_directory, false, true); // user mode, writable! not good!
		}

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

#if HEAP_DEBUG >= 3
	printk("init_paging() just finished; here's the current heap index\n");
	print_heap_index();
#endif
	init_double_fault_handler(kernel_directory);
}

extern struct idt_entry idt[256];

/* Loads the page directory at /dir/ into the CR3 register. */
void switch_page_directory(page_directory_t *dir) {
	INTERRUPT_LOCK;
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

	INTERRUPT_UNLOCK;
}

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
