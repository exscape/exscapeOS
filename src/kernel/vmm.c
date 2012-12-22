#include <kernel/vmm.h>
#include <kernel/interrupts.h>
#include <sys/types.h>
#include <string.h> /* memset */
#include <kernel/kernutil.h> /* panic */
#include <kernel/heap.h>
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

// Forward declarations; the internal (static, _ prefixed) functions are below the public ones
static void _vmm_invalidate(void *addr);
static void _vmm_map(uint32 virtual, uint32 physical, page_directory_t *dir, bool kernelmode, bool writable);
static page_t *_vmm_get_page(uint32 virtual, page_directory_t *dir);
static void _vmm_create_page_table(uint32 pt_index, page_directory_t *dir);
//static void _vmm_flush_tlb(void);

// Allocate physical memory for kernel space, possibly with continuous physical addresses, and map it to the selected virtual address range
// Returns the first physical address in the range (even if the range may be discontinuous).
uint32 vmm_alloc_kernel(uint32 start_virtual, uint32 end_virtual, bool continuous_physical, bool writable) {
	assert(end_virtual > start_virtual);
	assert((start_virtual & 0xfff) == 0);
	assert((end_virtual & 0xfff) == 0);
	assert(((end_virtual - start_virtual) & 0xfff) == 0); // Should follow from the above, but eh
	assert(continuous_physical == !!continuous_physical);
	assert(writable == !!writable);

	// No interrupt "locking" is necessary since all called functions take care of that,
	// and frame allocation + page mapping don't need to be atomic

	if (continuous_physical) {
		const uint32 ret = pmm_alloc_continuous((end_virtual - start_virtual) / PAGE_SIZE /* size */);
		uint32 phys = ret;
		for (uint32 addr = start_virtual; addr < end_virtual; addr += PAGE_SIZE, phys += PAGE_SIZE) {
			vmm_map_kernel(addr, phys, writable);
		}
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
		return ret;
	}
}

// Allocate physical memory for user space and map it to the selected virtual address range
void vmm_alloc_user(uint32 start_virtual, uint32 end_virtual, page_directory_t *dir, bool writable) {
	assert(end_virtual > start_virtual);
	assert((start_virtual & 0xfff) == 0);
	assert((end_virtual & 0xfff) == 0);
	assert(((end_virtual - start_virtual) & 0xfff) == 0);
	assert(writable == !!writable);

	// No interrupt "locking" is necessary since all called functions take care of that,
	// and frame allocation + page mapping don't need to be atomic

	for (uint32 addr = start_virtual; addr < end_virtual; addr += PAGE_SIZE) {
		uint32 phys = pmm_alloc();
		_vmm_map(addr, phys, dir, false /* user mode */, writable);
	}
}

// Unmap a virtual address. Does NOT free the associated physical memory (see vmm_free for that)
void vmm_unmap(uint32 virtual, page_directory_t *dir) {
	assert(dir != NULL);
	page_t *page = _vmm_get_page(virtual, dir);
	assert(page != NULL);
	assert(page->present == 1 || page->guard == 1);
	*((uint32 *)page) = 0;

	_vmm_invalidate((void *)virtual);
}

// Unmap a virtual address, and free its associated physical memory
void vmm_free(uint32 virtual, page_directory_t *dir) {
	assert(dir != NULL);
	pmm_free(vmm_get_phys(virtual, dir));
	vmm_unmap(virtual, dir);
}

// Calculate and return the physical address for a given virtual one
uint32 vmm_get_phys(uint32 virtual, page_directory_t *dir) {
	assert(dir != NULL);
	page_t *page = _vmm_get_page(virtual, dir);
	assert(page != NULL);
	uint32 phys = page->frame * PAGE_SIZE;
	phys += (virtual & 0xfff); // add the offset into the page

	return phys;
}

// Set/clear a guard page
static void _vmm_set_guard(uint32 virtual, page_directory_t *dir, bool guard /* true to set, false to clear */) {
	assert(dir != NULL);
	assert(guard == !!guard);

	page_t *page = _vmm_get_page(virtual, dir);
	INTERRUPT_LOCK;
	assert(page->present == guard); // if guard is true: should be true (before), else false
	assert(page->guard == !guard);  // if guard is true: should be false (before), else true
	page->present = !guard;
	page->guard = guard; // custom bit from the "avail" bits

	_vmm_invalidate((void *)virtual);
	INTERRUPT_UNLOCK;
}

void vmm_set_guard(uint32 virtual, page_directory_t *dir) {
	_vmm_set_guard(virtual, dir, true);
}

void vmm_clear_guard(uint32 virtual, page_directory_t *dir) {
	_vmm_set_guard(virtual, dir, false);
}

// Map a virtual address to a physical one, in kernel space, e.g. for MMIO
void vmm_map_kernel(uint32 virtual, uint32 physical, bool writable) {
	_vmm_map(virtual, physical, kernel_directory, true /* kernel mode */, writable);
}

size_t user_strlen(const char *p) {
	size_t len = 0;
	for (; *p != 0; len++, p++) { }
	return len;
}

// Internal function: map a virtual address to a physical one, for kernel- or userspace
static void _vmm_map(uint32 virtual, uint32 physical, page_directory_t *dir, bool kernelmode, bool writable) {
	assert(dir != NULL);
	assert(kernelmode == !!kernelmode);
	assert(writable == !!writable);

	page_t *page = _vmm_get_page(virtual, dir);

	INTERRUPT_LOCK;
	assert(*((uint32 *)page) == 0);

	page->frame = (physical / PAGE_SIZE);
	page->user = !kernelmode;
	page->rw = !!writable;
	page->present = 1;

	if (IS_KERNEL_SPACE(virtual)) {
		// We use this macro instead of the /kernelmode/ parameter because at the moment,
		// not all kernel structures are mapped as kernel mode (unfortunately).
		// We still want this to stay in the TLB despite task/page dir switches though.
		page->global = 1;
	}

	_vmm_invalidate((void *)virtual);
	INTERRUPT_UNLOCK;
}

// Internal function: get a pointer to a page entry; create the page table containing it if necessary
static page_t *_vmm_get_page(uint32 virtual, page_directory_t *dir) {
	assert(dir != NULL);

	// 4096: page size; 1024: number of pages per page table
	uint32 pt_index = virtual / 4096 / 1024;
	uint32 pt_offset = (virtual / 4096) % 1024;

	INTERRUPT_LOCK;
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
	assert(interrupts_enabled() == false);

	assert(dir->tables[pt_index] == NULL);

	uint32 phys;
	dir->tables[pt_index] = (page_table_t *)kmalloc_ap(sizeof(page_table_t), &phys);
	memset(dir->tables[pt_index], 0, sizeof(page_table_t));
	dir->tables_physical[pt_index] = phys | PTE_PRESENT | PTE_USER | PTE_RW; // TODO: access bits are currently used on a page-level only

	if (IS_KERNEL_SPACE(pt_index * 4096 * 1024)) {
		assert(dir == kernel_directory);
		  // This belongs to kernel space, and needs to be in sync across
			// *ALL* user mode tasks as well
			for (node_t *it = pagedirs->head; it != NULL; it = it->next) {
				page_directory_t *d = (page_directory_t *)it->data;
				if (d != kernel_directory && d != 0 && d != dir) {
					d->tables[pt_index] = dir->tables[pt_index];
					d->tables_physical[pt_index] = dir->tables_physical[pt_index];

					panic("updated task's page dir (dir 0x%08x); page table = 0x%08x - TODO: test this!\n", d, dir->tables);
				}
			}
	}
}

void *sbrk(sint32 incr) {
	// Adds (at least) /incr/ bytes (negative values subtract) to this task's heap area.
	// The initial heap area is always zero.
	// Returns the *previous* break value, i.e. the start of the newly allocated region.
	// (Or, if incr == 0, the current break value, since the new and the old are equal.)
	assert(current_task->privilege == 3);
	assert(current_task->mm != NULL);
	struct task_mm *mm = current_task->mm;

	uint32 prev_brk = mm->brk;

	assert(IS_PAGE_ALIGNED(mm->brk));

	if (incr == 0) {
		// Return the current break value
		return (void *)mm->brk;
	}

	if (incr > 0) {
		uint32 new_end = mm->brk + incr;
		if (!IS_PAGE_ALIGNED(new_end)) {
			new_end &= 0xfffff000;
			new_end += PAGE_SIZE;
		}

		assert(new_end < 0xb0000000); // TODO: use the actual user stack location here

		vmm_alloc_user(mm->brk, new_end, current_task->page_directory, PAGE_RW);

		// TODO: this should REALLY be in a separate function, or taken care of by vmm_alloc_user
		addr_entry_t *entry = NULL;
		INTERRUPT_LOCK;
		for (node_t *it = mm->pages->head; it != NULL; it = it->next) {
			addr_entry_t *e = (addr_entry_t *)it->data;
			if ((uint32)e->start == mm->brk_start) {
				entry = e;
				break;
			}
		}
		INTERRUPT_UNLOCK;

		if (entry == NULL) {
			// This is the first call to sbrk
			assert(mm->brk_start == mm->brk);
			entry = kmalloc(sizeof(addr_entry_t));
			entry->start = (void *)mm->brk_start;
			assert(IS_PAGE_ALIGNED(new_end - mm->brk));
			entry->num_pages = (new_end - mm->brk) / PAGE_SIZE;

			list_append(mm->pages, entry);
		}
		else {
			assert(IS_PAGE_ALIGNED(new_end - mm->brk));
			entry->num_pages += (new_end - mm->brk) / PAGE_SIZE;
		}

		mm->brk = new_end;

		assert(IS_PAGE_ALIGNED(mm->brk));
		assert(IS_PAGE_ALIGNED(prev_brk));

		return (void *)prev_brk;
	}
	else {
		// Caller wants to decrease the heap
		panic("sbrk with negative argument: TODO"); // TODO
		return 0;
	}
}

void init_double_fault_handler(page_directory_t *pagedir_addr);

static void enable_paging(void) {
	INTERRUPT_LOCK;

	/*
	 * This seems silly to do in two steps, but it's
	 * done because of this little paragraph from the Intel manual (aka. the holy
	 * OSdev bible):
	 *   "When enabling the global page feature, paging must be enabled (by
	      setting the PG flag in control register CR0) before the PGE flag is set.
		  Reversing this sequence may affect program correctness, and processor
		  performance will be impacted."
	*
	* They mention "reversing" the sequence, not doing it simultaneously, but
	* to be on the safe side, do it in two steps.
	*/
	asm volatile("mov %%cr0, %%eax;"
				 "or $0x80000000, %%eax;" /* PG = 1 */
				 "mov %%eax, %%cr0;"
				 "mov %%cr0, %%eax;" /* I assume this isn't really needed, but eh */
				 "or $0x00000080, %%eax;" /* PGE = 1 */
				 "mov %%eax, %%cr0;"
				 : /* no outputs */
				 : /* no inputs */
				 : "%eax", "cc");
	INTERRUPT_UNLOCK;
}

extern uint32 __start_text;
extern uint32 __end_text;

void _pmm_set_frame(uint32 phys_addr);

/* Sets up everything required and activates paging. */
void init_paging(unsigned long mbd_mmap_addr, unsigned long mbd_mmap_length, unsigned long upper_mem) {
	assert(sizeof(page_t) == 4);

	pmm_init(mbd_mmap_addr, mbd_mmap_length, upper_mem);

	pagedirs = list_create();

	/* Create a page directory */
	kernel_directory = (page_directory_t *)kmalloc_a(sizeof(page_directory_t));
	memset(kernel_directory, 0, sizeof(page_directory_t));
	kernel_directory->physical_address = (uint32)kernel_directory->tables_physical;

	list_append(pagedirs, kernel_directory); // TODO: should this be the one left out of the list?

	INTERRUPT_LOCK;

	/* Create ALL the page tables that may be necessary for the kernel heap.
	 * This way, we cannot run in to the godawful situation of malloc() -> heap full -> heap_expand() -> get_page() -> malloc() new page table (while heap is full!)! */
	for (uint32 index = (KHEAP_START / PAGE_SIZE / 1024); index <= (KHEAP_MAX_ADDR / PAGE_SIZE / 1024); index++) {
		_vmm_create_page_table(index, kernel_directory);
	}

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

	// Map the virtual addresses for the kernel, with their respective permissions
	uint32 addr = 0x100000;
	while (addr < placement_address + PAGE_SIZE) {
		if (addr >= start_text && addr < end_text) {
			// This is a kernel .text page - map it as read-only
			vmm_map_kernel(addr, addr, PAGE_RO);
			_pmm_set_frame(addr);
		}
		else {
			// Kernel data - mark it as read-write
			vmm_map_kernel(addr, addr, PAGE_RW);
			_pmm_set_frame(addr);
		}

		addr += PAGE_SIZE;
	}
	INTERRUPT_UNLOCK;

	/* Map the video RAM region (one page is enough for what we use) */
	_vmm_map(0xb8000, 0xb8000, kernel_directory, true, PAGE_RW);

	// Set address 0 as a guard page, to catch NULL pointer dereferences
	_vmm_map(0, 0, kernel_directory, true /* kernel mode */, PAGE_RO);
	vmm_set_guard(0 /* address */, kernel_directory);

	/*
	 * Low addresses, i.e. addresses < 0x100000, are used for allocations,
	 * such as the one below, if we got a memory map from GRUB. If we didn't,
	 * they are ignored.
	 */

	/* Register the page fault handler */
	register_interrupt_handler(EXCEPTION_PAGE_FAULT, page_fault_handler);

	/* Enable paging! */
	switch_page_directory(kernel_directory);
	enable_paging();

	/* Initialize the kernel heap */
	kheap = heap_create(KHEAP_START, KHEAP_INITIAL_SIZE, KHEAP_MAX_ADDR, 1, 0, kernel_directory); /* supervisor, not read-only */

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
				 : /* no outputs */
				 : "r"(new_cr3_contents)
				 : "%eax", "cc");

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
// *NOTE*: this doesn't flush EVERYTHING: global pages are untouched.
// Clear and set CR4.PGE instead?
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

	panic("Page fault!");

	return esp; /* not reached at the moment */
}
