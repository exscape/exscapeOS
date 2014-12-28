#include <kernel/vmm.h>
#include <kernel/interrupts.h>
#include <sys/types.h>
#include <string.h> /* memset */
#include <kernel/kernutil.h> /* panic */
#include <kernel/heap.h>
#include <kernel/console.h> /* printk */
#include <kernel/pmm.h>
#include <kernel/elf.h> /* symbol lookup */

// The kernel's page directory
page_directory_t *kernel_directory = 0;
// The current page directory;
page_directory_t *current_directory = 0;

// Forward declarations; the internal (static, _ prefixed) functions are below the public ones
static void _vmm_invalidate(void *addr);
static void _vmm_map(uint32 virtual, uint32 physical, page_directory_t *dir, bool kernelmode, bool writable);
static page_t *_vmm_get_page(uint32 virtual, page_directory_t *dir);
static void _vmm_create_page_table(uint32 pt_index, page_directory_t *dir);
//static void _vmm_flush_tlb(void);

/* defined in kheap.c */
extern uint32 placement_address;
extern heap_t *kheap;

uint32 mem_end_page = 0;

list_t *pagedirs = NULL;

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

static void _vmm_store_area(uint32 start_virtual, uint32 end_virtual, struct task_mm *mm, bool writable) {
	assert(mm != NULL);
	assert(mm->areas != NULL);

	INTERRUPT_LOCK;

	// Check to see if this is an extension to an existing area, with the same
	// access rights
	list_foreach(mm->areas, it) {
		assert(interrupts_enabled() == false);
		vm_area_t *a = (vm_area_t *)it->data;

		if (a->start == (void *)end_virtual && a->writable == writable) {
			// Yes, this allocation is just before the previous area; extend it
			a->start = (void *)start_virtual;
			goto done;
		}
		else if (a->end == (void *)start_virtual && a->writable == writable) {
			// Yes, this allocation is just after the previous area; extend it
			a->end = (void *)end_virtual;
			goto done;
		}
		else if ( (start_virtual >= (uint32)a->start && start_virtual < (uint32)a->end) || /* new area starts inside old area */ \
				  (end_virtual > (uint32)a->start && end_virtual < (uint32)a->end))       /* new area ends inside old area */
		{
			panic("VM areas overlap! Old area: %p-%p, new area: %p-%p\n", a->start, a->end, (void *)start_virtual, (void *)end_virtual);
		}
	}

	// If we are still here, this is NOT an extension, but a new area. Store it.
	vm_area_t *area = kmalloc(sizeof(vm_area_t));
	memset(area, 0, sizeof(vm_area_t));
	area->start = (void *)start_virtual;
	area->end   = (void *)end_virtual;
	area->writable = writable;
	list_append(mm->areas, area);

done:
	INTERRUPT_UNLOCK;
	return;
}

void vmm_resize_area(uint32 start_virtual, uint32 new_end_virtual, struct task_mm *mm) {
	// Grow or shrink the area that starts at (or contains!) start_virtual, to end at new_end_virtual.
	// Note that growing can also be done by using vmm_alloc_user on the end address, e.g.
	// to grow the range that spans [a, b), call vmm_alloc_user(b, c, ...), and the area
	// will be expanded (a new area will not be written).

	assert(new_end_virtual >= start_virtual + PAGE_SIZE);
	assert(mm != NULL);
	assert(mm->areas != NULL);

	list_foreach(mm->areas, it) {
		vm_area_t *area = (vm_area_t *)it->data;
		uint32 area_end = (uint32)area->end;
		assert(area_end != new_end_virtual);

		if ((uint32)area->start <= start_virtual && (uint32)area->end > start_virtual) {
			// Found it
			if (new_end_virtual > area_end) {
				// Grow the area
				vmm_alloc_user(area_end, new_end_virtual, mm, area->writable);
			}
			else {
				// Shrink it
				for (uint32 i = new_end_virtual; i < area_end; i += PAGE_SIZE) {
					vmm_free(i, mm->page_directory);
					mm->frames_used--;
				}
				area->end = (void *)new_end_virtual;
			}
			break;
		}
	}
}

// Allocate physical memory for user space and map it to the selected virtual address range
void vmm_alloc_user(uint32 start_virtual, uint32 end_virtual, struct task_mm *mm, bool writable) {
	assert(end_virtual > start_virtual);
	assert((start_virtual & 0xfff) == 0);
	assert((end_virtual & 0xfff) == 0);
	assert(((end_virtual - start_virtual) & 0xfff) == 0);
	assert(writable == !!writable);
	assert(mm != NULL);

	// No interrupt "locking" is necessary since all called functions take care of that,
	// and frame allocation + page mapping don't need to be atomic

	for (uint32 addr = start_virtual; addr < end_virtual; addr += PAGE_SIZE) {
		uint32 phys = pmm_alloc();
		mm->frames_used++;
		_vmm_map(addr, phys, mm->page_directory, false /* user mode */, writable);
	}

	// Store information about this allocation
	_vmm_store_area(start_virtual, end_virtual, mm, writable);
}

void vmm_destroy_task_mm(struct task_mm *mm) {
	assert(mm != NULL);

	INTERRUPT_LOCK;
	if (mm->areas != NULL) {
		// Only userspace tasks use this member
		list_foreach(mm->areas, it) {
			vm_area_t *area = (vm_area_t *)it->data;
			for (uint32 addr = (uint32)area->start; addr < (uint32)area->end; addr += PAGE_SIZE) {
				vmm_free(addr, mm->page_directory);
				// There's little point in reducing frames_used here, since we destroy the mm anyway
			}
			kfree(area);
		}
		list_destroy(mm->areas);
	}

	kfree(mm);
	INTERRUPT_UNLOCK;
}

void copy_page_physical(uint32 src, uint32 dst);

page_directory_t *clone_user_page_directory(page_directory_t *parent_dir, struct task_mm *child_mm) {
	assert(parent_dir != NULL);
	assert(child_mm != NULL);

	uint32 new_dir_phys;
	page_directory_t *child_dir = kmalloc_ap(sizeof(page_directory_t), &new_dir_phys);
	memset(child_dir, 0, sizeof(page_directory_t));

	INTERRUPT_LOCK;
	for (int i = 0; i < 1024; i++) {
		if (IS_KERNEL_SPACE(i * 1024 * PAGE_SIZE)) {
			// The page table for this region should be linked to the same table as for
			// all other tasks, as the kernel space is the same in all of them
			child_dir->tables_physical[i] = parent_dir->tables_physical[i];
			child_dir->tables[i] = parent_dir->tables[i];
		}
		else {
			// This page table and all of its contents should be copied -- if there's anything here
			if (parent_dir->tables[i] == NULL)
				continue;

			_vmm_create_page_table(i, child_dir);
			//printk("copying user space range %p-%p\n", i * 1024 * PAGE_SIZE, (i+1) * 1024*PAGE_SIZE - 1);
			for (int j = 0; j < 1024; j++) {
				page_t *page_orig = &parent_dir->tables[i]->pages[j];
				if (page_orig->frame != 0) {
					page_t *page_copy = &child_dir->tables[i]->pages[j];
					uint32 phys = pmm_alloc();
					child_mm->frames_used++;
					copy_page_physical(page_orig->frame * PAGE_SIZE, phys); // copy the actual data for this page
					*(uint32 *)page_copy = *(uint32 *)page_orig; // copy the page flags, etc.
					page_copy->frame = phys / PAGE_SIZE;
				}
			}
		}
	}
	list_append(pagedirs, child_dir);
	INTERRUPT_UNLOCK;

	/*
	 * We need the physical address of the /tables_physical/ struct member. /dir/ points to the beginning of the structure, of course.
	 * Since the physical address is (obviously!) in another address space, we can't use the & operator, but must instead calculate
	 * its offset into the structure, then add that to the physical address.
	 */
	uint32 offset = (uint32)child_dir->tables_physical - (uint32)child_dir;
	child_dir->physical_address = new_dir_phys + offset;

	return child_dir;
}

void *copy_area(void *_area) {
	vm_area_t *area = (vm_area_t *)_area;
	vm_area_t *area_copy = kmalloc(sizeof(vm_area_t));
	memcpy(area_copy, area, sizeof(vm_area_t));

	return area_copy;
}

struct task_mm *vmm_clone_mm(struct task_mm *parent_mm) {
	assert(parent_mm != NULL);
	assert(parent_mm->areas != NULL);

	struct task_mm *child_mm = kmalloc(sizeof(struct task_mm));
	memset(child_mm, 0, sizeof(struct task_mm));

	child_mm->areas = list_copy(parent_mm->areas, copy_area);
	child_mm->page_directory = clone_user_page_directory(parent_mm->page_directory, child_mm);

	return child_mm;
}

bool vmm_check_access_write(uint32 addr, uint32 len) {
	// Checks whether the current userspace process has write access to the memory range of
	// [addr, addr + len)
	// Previous checks should have figured out that this is in userspace.
	assert(current_task->privilege == 3);
	struct task_mm *mm = current_task->mm;
	assert(mm != NULL);

	INTERRUPT_LOCK;
	list_foreach(mm->areas, it) {
		vm_area_t *a = (vm_area_t *)it->data;
		if (a->writable == false)
			continue;

		if (addr >= (uint32)a->start && addr < (uint32)a->end) {
			// The beginning of this area is valid; is all of it valid?
			if (addr + len < (uint32)a->end) {
				// Yes!
				INTERRUPT_UNLOCK;
				return true;
			}
		}
	}
	INTERRUPT_UNLOCK;

	return false;
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
		list_foreach(pagedirs, it) {
			page_directory_t *d = (page_directory_t *)it->data;
			if (d != kernel_directory && d != 0 && d != dir) {
				d->tables[pt_index] = dir->tables[pt_index];
				d->tables_physical[pt_index] = dir->tables_physical[pt_index];

				panic("updated task's page dir (dir 0x%08x); page table = 0x%08x - TODO: test this!\n", d, dir->tables);
			}
		}
	}
}

struct task_mm *vmm_create_user_mm(void) {
	// Set up the memory map struct for this task
	struct task_mm *mm = kmalloc(sizeof(struct task_mm));
	memset(mm, 0, sizeof(struct task_mm));
	mm->areas = list_create();
	mm->page_directory = create_user_page_dir();

	mm->frames_used = 0;

	/* Set up a usermode stack for this task */
	vmm_alloc_user(USER_STACK_START - (USER_STACK_SIZE + PAGE_SIZE), USER_STACK_START + PAGE_SIZE, mm, PAGE_RW);

	/* Set a guard page */
	vmm_set_guard(USER_STACK_START - (USER_STACK_SIZE + PAGE_SIZE), mm->page_directory);

	/* This is used by the code that expands the user stack on page fault, up to a limit */
	mm->user_stack_guard_page = USER_STACK_START - (USER_STACK_SIZE + PAGE_SIZE);

	return mm;
}

struct task_mm *vmm_create_kernel_mm(void) {
	// Set up the memory map struct for this task
	struct task_mm *mm = kmalloc(sizeof(struct task_mm));
	memset(mm, 0, sizeof(struct task_mm));
	mm->page_directory = current_directory;

	return mm;
}

void *sbrk(sint32 delta) {
	// Adds (at least) /delta/ bytes (negative values subtract) to this task's heap area.
	// The initial heap area is always zero.
	// Returns the *previous* break value, i.e. the start of the newly allocated region.
	// (Or, if delta == 0, the current break value, since the new and the old are equal.)
	assert(current_task->privilege == 3);
	assert(current_task->mm != NULL);
	struct task_mm *mm = current_task->mm;

	uint32 prev_brk = mm->brk;

	assert(IS_PAGE_ALIGNED(mm->brk));

	if (delta == 0) {
		// Return the current break value
		return (void *)mm->brk;
	}

	if (delta > 0) {
		uint32 new_end = mm->brk + delta;
		if (!IS_PAGE_ALIGNED(new_end)) {
			new_end &= 0xfffff000;
			new_end += PAGE_SIZE;
		}

		assert(new_end < USER_STACK_START - USER_STACK_SIZE_MAX);
		vmm_alloc_user(mm->brk, new_end, mm, PAGE_RW);

		mm->brk = new_end;

		assert(IS_PAGE_ALIGNED(mm->brk));
		assert(IS_PAGE_ALIGNED(prev_brk));

		return (void *)prev_brk;
	}
	else {
		uint32 decr = -delta;
		// Caller wants to decrease the heap
		if (decr < PAGE_SIZE) {
			// Nothing we can do for such a small decrease.
			return (void *)prev_brk;
		}

		if (decr & 0xfff) {
			// If they want to decrease e.g. 6325 bytes, we only do 4096;
			// if they want 10495, we do 8192, and so on.
			// We can't decrease *more* than they're asking, so the largest
			// multiple of the page size will have to do.
			decr &= 0xfffff000;
		}

		assert(decr > 0);
		assert(prev_brk - decr >= mm->initial_brk);
		assert(IS_PAGE_ALIGNED(prev_brk - decr));

		vmm_resize_area(mm->brk_start, prev_brk - decr, mm);
		mm->brk = prev_brk - decr;

		return (void *)prev_brk;
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
		// DON'T add PAGE_SIZE here, we want to truncate to map it all (.text starts at 0x10000c atm)
	}
	if ((end_text & 0xfff) != 0) {
		end_text &= 0xfffff000;
		end_text += PAGE_SIZE;
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
	kheap = heap_create(KHEAP_START, KHEAP_INITIAL_SIZE, KHEAP_MAX_ADDR, 1, 0, NULL); /* supervisor, not read-only; mm arg is NULL for the kernel */

	placement_address = 0; // This must not be used after this!

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
	list_remove_first(pagedirs, dir);

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

	// If the conditions are right, grow the userspace stack, if this page fault occurred on the guard page.
	struct task_mm *mm = current_task->mm;
	if (current_task->privilege != 3 || current_task-> mm == NULL)
		goto fail; /* Heh... should I use brackets here, perhaps? :-) */
	if (!(faulting_address >= mm->user_stack_guard_page && faulting_address <= mm->user_stack_guard_page + PAGE_SIZE))
		goto fail;

	// OK, so it did fail on the guard page.
	// Is the stack size already at the maximum limit (rather, will we *exceed* the limit if we grow?)?
	uint32 old_size = USER_STACK_START - mm->user_stack_guard_page - PAGE_SIZE;
	uint32 new_size = old_size + USER_STACK_GROW_SIZE;
	assert((old_size & 0xfff) == 0);
	assert((new_size & 0xfff) == 0);
	if (new_size >= USER_STACK_SIZE_MAX) {
		printk("Warning: userspace stack overflow, with no further space to grow!\n");
		goto fail;
	}

	// OK, we should grow it!
//	printk("Growing userspace stack! Old size: %d, new size: %d\n", old_size, new_size);
//	printk("                         Old top: %p, new top: %p\n", USER_STACK_START - old_size, USER_STACK_START - new_size);

	/* Actually grow the area; vmm_alloc_user updates/grows the area metadata for us, also */
	vmm_alloc_user(USER_STACK_START - (new_size + PAGE_SIZE), USER_STACK_START - (old_size + PAGE_SIZE), mm, PAGE_RW);

	/* Move the guard page */
	vmm_clear_guard(USER_STACK_START - (old_size + PAGE_SIZE), mm->page_directory);
	vmm_set_guard(USER_STACK_START - (new_size + PAGE_SIZE), mm->page_directory);
	mm->user_stack_guard_page = USER_STACK_START - (new_size + PAGE_SIZE);

	return esp;

fail:
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

	dump_regs_and_bt(esp);

	printk("Heap end address is %p\n", kheap->end_address);

	panic("Page fault!");

	return esp; /* not reached at the moment */
}
