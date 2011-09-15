#include <kernel/kheap.h>
#include <kernel/kernutil.h>
#include <kernel/paging.h>
#include <kernel/monitor.h> /* for print_heap_index() */
#include <stdio.h> /* sprintf */
#include <string.h> /* memset */

/* Enable extra debugging, i.e. much stricter checks for each alloc/free */
#define HEAP_DEBUG 1

/* The kernel heap */
heap_t *kheap = NULL;

/* paging.c */
extern page_directory_t *kernel_directory;

/* Defined in linker.ld */
extern uint32 end;

uint32 placement_address = (uint32)&end;

/*
static sint8 area_t_less_than(void *a, void*b) {
	return ( ((area_t *)a)->header->size < ((area_t *)b)->header->size ) ? 1 : 0;
}
*/
heap_t *create_heap(uint32 start_address, uint32 initial_size, uint32 max_size, uint8 supervisor, uint8 readonly) {
	return 0;
}


/* Allocate /size/ bytes of memory.
 * If align == true, the return value will be page aligned.
 * If phys isn't NULL, it will be set to the physical address of the allocated area.
 *
 * Do note that there is no kfree().
 */
uint32 kmalloc_int(uint32 size, bool align, uint32 *phys) {
/*	if (kheap != NULL) {
		void *addr = alloc(size, align, kheap);
		if (phys != 0) {
			page_t *page = get_page((uint32)addr, 0, kernel_directory);
			*phys = (page->frame * 0x1000) + ((uint32)addr & 0xfff);
		}
		return (uint32)addr;
	}
	else { */
		/* kheap == NULL, i.e. we haven't created the kernel heap yet */

		if (align == true && ((placement_address & 0xffffff000)) != 0) {
			/* The page isn't page aligned (the lower 12 bits aren't all-zero */
			placement_address &= 0xfffff000; /* align it to a page boundary */
			placement_address += 0x1000;     /* ... the NEXT page boundary */
		}

		if (phys != 0) {
			/* The caller wants to know the physical placement address. */
			*phys = placement_address;
		}

		/* Save the address */
		uint32 ret = placement_address;

		/* Move the pointer, for the next call */
		placement_address += size;

		return ret;
//	}
}

/*
void kfree(void *p) {
	free(p, kheap);
}
*/

/* Plain kmalloc; not page-aligned, doesn't return the physical address */
uint32 kmalloc(uint32 size) {
	return kmalloc_int(size, false, NULL);
}

/* Page-aligned kmalloc */
uint32 kmalloc_a(uint32 size) {
	return kmalloc_int(size, true, NULL);
}

/* Returns the physical address in phys, but doesn't page align. */
uint32 kmalloc_p(uint32 size, uint32 *phys) {
	return kmalloc_int(size, false, phys);
}

/* Returns the physical address in phys, and page aligns. */
uint32 kmalloc_ap(uint32 size, uint32 *phys) {
	return kmalloc_int(size, true, phys);
}
