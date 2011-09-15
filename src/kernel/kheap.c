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

static sint8 area_header_t_less_than(void *a, void*b) {
	return ( ((area_header_t *)a)->size < ((area_header_t *)b)->size ) ? 1 : 0;
}

void print_heap_index(void) {
	panic("Implement me!");
}

void do_asserts_for_index(ordered_array_t *index, area_header_t *header_to_create, area_footer_t *footer_to_create, uint32 size) {
	assert(index != NULL);
	assert(header_to_create != NULL);
	assert(footer_to_create != NULL);

	for (int i = 0; i < index->size; i++) {
		area_header_t *found_header = (area_header_t *)lookup_ordered_array(i, index);
		area_footer_t *found_footer = FOOTER_FROM_HEADER(found_header);

		/* Equality tests */
		assert(found_header != header_to_create);
		assert(found_footer != footer_to_create);
		assert((uint32)found_header != (uint32)footer_to_create);
		assert((uint32)found_footer != (uint32)header_to_create);

		/* A bonus: validate the already-existing area */
		assert(found_header->magic == HEAP_MAGIC);
		assert(found_footer->magic == HEAP_MAGIC);
		assert(found_footer->header == found_header);

		/* Ensure that we don't end up INSIDE the address space of another area */
		if (header_to_create < found_header) {
			/* The header we found is more to the right than us. Make sure that our ENTIRE SPACE is to the left of it */
			assert((uint32)header_to_create + size <= (uint32)found_header);
		}
		else if (header_to_create > found_header) {
			/* We're to the right; make sure we don't write inside the area that begins at found_header */
			assert((uint32)found_header + found_header->size <= (uint32)header_to_create);
		}

	}
}

static area_header_t *create_block(uint32 address, uint32 size, uint8 type, heap_t *heap) {
	/* This function should only be called AFTER the heap has been set up! */
	assert(kheap != NULL);
	assert(type == AREA_USED || type == AREA_FREE);
	
	/* The caller of this function is responsible for details like page alignment.
	 * If we should page-align something, then address should BE page aligned by now. */

	area_header_t *header_to_create = (area_header_t *) address;
	area_footer_t *footer_to_create = (area_footer_t *) (address + size - sizeof(area_footer_t) );

#if HEAP_DEBUG
	/* Very expensive, but very useful sanity checks! */
	/* Due to the fact that we have TWO indexes, move this to a separate function */
	do_asserts_for_index(&kheap->free_index, header_to_create, footer_to_create, size);
	do_asserts_for_index(&kheap->used_index, header_to_create, footer_to_create, size);
#endif

	header_to_create->size = size;
	header_to_create->magic = HEAP_MAGIC;
	header_to_create->type = type;
	
	footer_to_create->magic = HEAP_MAGIC;
	footer_to_create->header = header_to_create;

	return header_to_create;
}

heap_t *create_heap(uint32 start_address, uint32 initial_size, uint32 max_address, uint8 supervisor, uint8 readonly) {
	heap_t *heap = (heap_t *)kmalloc_a(sizeof(heap_t));
	assert (heap != NULL);

	/* Start and end addresses need to be page aligned; the end address is calculated and checked below */
	assert(IS_PAGE_ALIGNED(start_address));

	/* Create the indexes; they are zeroed in place_ordered_array */
	heap->used_index = place_ordered_array((void *)start_address, HEAP_INDEX_SIZE, &area_header_t_less_than);
	start_address += HEAP_INDEX_SIZE * sizeof(type_t);
	heap->free_index = place_ordered_array((void *)start_address, HEAP_INDEX_SIZE, &area_header_t_less_than);
	start_address += HEAP_INDEX_SIZE * sizeof(type_t);

	/* Reduce the size of the actual heap to account for the indexes */
	initial_size -= HEAP_INDEX_SIZE * sizeof(type_t) * 2;

	/* Make sure the start address is page aligned, now that we've modified it */
	if (!IS_PAGE_ALIGNED(start_address)) {
		/* Page align it */
		start_address &= 0xfffff000;
		start_address += 0x1000;
	}

	uint32 end_address = start_address + initial_size;
	assert(IS_PAGE_ALIGNED(end_address));

	heap->start_address = start_address;
	heap->end_address   = end_address;
	heap->max_address   = max_address;
	heap->supervisor    = supervisor ? 1 : 0;
	heap->readonly      = readonly   ? 1 : 0;


	/* Create a free area that spans the entire heap */
	area_header_t *header_to_create = (area_header_t *)start_address;
	area_footer_t *footer_to_create = (area_footer_t *)( (uint32)start_address + initial_size - sizeof(area_footer_t) );

	header_to_create->size = initial_size;
	header_to_create->magic = HEAP_MAGIC;
	header_to_create->type = AREA_FREE;
	
	footer_to_create->magic = HEAP_MAGIC;
	footer_to_create->header = header_to_create;

	insert_ordered_array((void *)header_to_create, &heap->free_index);
	
	return heap;
}


/* Allocate /size/ bytes of memory.
 * If align == true, the return value will be page aligned.
 * If phys isn't NULL, it will be set to the physical address of the allocated area.
 *
 * Do note that there is no kfree().
 */
void *kmalloc_int(uint32 size, bool align, uint32 *phys) {
	if (kheap != NULL) {
		panic("fixme!");
		create_block(0, 0, 0, 0);
	}
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

		if (align == true && !IS_PAGE_ALIGNED(placement_address)) {
			/* The page isn't page aligned; align it */
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

		return (void *)ret;
//	}
}

/*
void kfree(void *p) {
	free(p, kheap);
}
*/

/* Plain kmalloc; not page-aligned, doesn't return the physical address */
void *kmalloc(uint32 size) {
	return kmalloc_int(size, false, NULL);
}

/* Page-aligned kmalloc */
void *kmalloc_a(uint32 size) {
	return kmalloc_int(size, true, NULL);
}

/* Returns the physical address in phys, but doesn't page align. */
void *kmalloc_p(uint32 size, uint32 *phys) {
	return kmalloc_int(size, false, phys);
}

/* Returns the physical address in phys, and page aligns. */
void *kmalloc_ap(uint32 size, uint32 *phys) {
	return kmalloc_int(size, true, phys);
}
