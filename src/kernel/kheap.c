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

void validate_heap_index(bool print_areas) {
	/* Since there are two indexes, we need to loop through them both! */
	ordered_array_t *indexes[] = { &kheap->used_index, &kheap->free_index };
	for (int index_num=0; index_num < 2; index_num++) {
		/* The index we're working with right now */
		ordered_array_t *index = indexes[index_num];

		for (int i=0; i < index->size; i++) {
			area_header_t *found_header = lookup_ordered_array(i, index);
			area_footer_t *found_footer = FOOTER_FROM_HEADER(found_header);

			if (index_num == 0) {
				/* This is used_index; make sure ALL areas from this index ARE USED, not free! */
				assert(found_header->type == AREA_USED);
			}
			else {
				/* This is free_index; make sure ALL areas from this index are FREE */
				assert(found_header->type == AREA_FREE);
			}

			if (print_areas) {
				printk("%s: %p to %p (%d bytes); %s pointer + magics\n",
						(found_header->type == AREA_USED ? "used" : "free"),
						(uint32)found_header,
						(uint32)found_header + found_header->size,
						found_header->size,
						(found_header->magic  == HEAP_MAGIC && found_footer->magic  == HEAP_MAGIC && found_footer->header == found_header) ? "valid" : "INVALID"
					  );
			}

			assert(found_header->magic == HEAP_MAGIC);
			assert(found_footer->magic == HEAP_MAGIC);
			assert(found_footer->header == found_header);
		}
	}
}

void print_heap_index(void) {
	/* Use the function above */
	printk("--------- Heap index ---------\n");
	validate_heap_index(/*print_areas = */true);
	printk("-------- End heap index ------\n");
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

static area_header_t *create_area(uint32 address, uint32 size, uint8 type, heap_t *heap) {
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

	/* Set up the area "manually", since create_area() requires the heap to be set up! */
	header_to_create->size = size;
	header_to_create->magic = HEAP_MAGIC;
	header_to_create->type = type;

	footer_to_create->magic = HEAP_MAGIC;
	footer_to_create->header = header_to_create;

	/* All done! */
	return header_to_create;
}

area_header_t *find_smallest_hole(uint32 size, bool page_align, heap_t *heap) {
	/* This function does what is says on the tin: loops through all the holes, and returns the first (=smallest) one that is still big enough. */

	area_header_t *header = NULL;

	/* Loop through all the free areas */
	for (int i = 0; i < kheap->free_index.size; i++) {
		header = lookup_ordered_array(i, &kheap->free_index);
#if KHEAP_DEBUG
	/* More checks never hurt! */
		assert(header->magic == HEAP_MAGIC);
		assert(header->type == AREA_FREE);
#endif

		if (header->size >= size) {
			/* We found something! */
			if (page_align) {
				/* TODO: Add support for page alignment! */
				   panic("find_smallest_hole(): page alignment not yet supported!");
			} 
			else {
				/* No page align, so we're good! */
				return header;
			}
		}
	}

	/* If the loop exited, we didn't find any hole that works! */
	return NULL;
}

void *heap_alloc(uint32 size, bool page_align, heap_t *heap) {
	/* Take the header and footer overhead into account! */
	size += sizeof(area_header_t) + sizeof(area_footer_t);

	area_header_t *area = find_smallest_hole(size, page_align, heap);

	if (area == NULL) {
		/* There were no holes big enough! Expand the heap. */
		/* Expand with at least HEAP_MIN_GROWTH bytes (either that or /size/ bytes, whichever is LARGER) */
		panic("Expand the heap here!");

		/* Then try again: */
		return heap_alloc(size, page_align, heap);
	} /* end area == NULL */

	if (page_align) {
		/* TODO: add page alignment support! */
		panic("TODO: add page_align support to heap_alloc");
		/* TODO: create a new, free area with the wasted space between /area/ and the page-aligned area we'll return */
	}

	/* Remove this area from the free_index */
	/* This must be done before the check to create a second free area (below), or that will fail,
	 * since the footer for the entire free area we're using will have the same footer location as the new free area.
	 * An assert() checks that to prevent errors, and will fail if we try to create that hole before we do this:
	 */
	remove_ordered_array_item((void *)area, &kheap->free_index);

	/* Is there enough space in the free area we're using to fit us AND another free area? */
	if (area->size - size >= 16 + sizeof(area_header_t) + sizeof(area_footer_t)) {
		/* The area we are allocating (/area/) is large enough to not only fit our data, but ANOTHER free area. */
		/* Create a new area of size (area->size - size), where /size/ is the user-requested size for the allocation. */

		/* The new free area is located /size/ (of the original allocation) bytes past the original area */
		area_header_t *free_space_header = (area_header_t *)( (uint32)area + size );

		/* Write the new free area to memory */
		create_area((uint32)free_space_header, (area->size - size), AREA_FREE, heap);

		/* Write it to the index */
		insert_ordered_array((void *)free_space_header, &kheap->free_index);
	}
	else {
		/* There's not enough space to bother making a new area.
		 * If there is ANY space (literally 1 byte or more), add it to the current allocation, instead. */
		   panic("Step through this with gdb! Does it work?");

		   size += (area->size - size);
	}

	/* Write the area to memory */
	create_area((uint32)area, size, AREA_USED, heap);

	/* Add this area to the used_index */
	insert_ordered_array((void *)area, &kheap->used_index);

	return (void *)( (uint32)area + sizeof(area_header_t) );
}

void heap_free(void *p, heap_t *heap) {
	/* free() of a NULL pointer should be valid */
	if (p == NULL)
		return;

	/* Calculate the header and footer locations */
	area_header_t *header = (area_header_t *)( (uint32)p - sizeof(area_header_t) );
	area_footer_t *footer = FOOTER_FROM_HEADER(header);

	/* Sanity checks */
	assert(header->magic == HEAP_MAGIC);
	assert(header->type == AREA_USED);
	assert(footer->magic == HEAP_MAGIC);
	assert(footer->header == header);

	/* Remove this area from the used index. We'll wait a little before adding it as a free area, though. */
	remove_ordered_array_item((void *)header, &kheap->used_index);

	/* Mark this area as free in memory */
	header->type = AREA_FREE;

	/* Should we add this hole to the index? No if we unify left. */
	bool add_to_index = true;

	/* Check if the area to our left is another free area; if so, merge with it, aka. unify left */
	area_footer_t *left_area_footer = (area_footer_t *)( (uint32)header - sizeof(area_footer_t) );
	if ((uint32)left_area_footer >= (uint32)heap->start_address && /* make sure to not make an invalid access */
		left_area_footer->magic == HEAP_MAGIC) {
		/* Looks like it! */
		   area_header_t *left_area_header = left_area_footer->header;
		   if (left_area_header->magic == HEAP_MAGIC && left_area_header->type == AREA_FREE) {
			   /* Yep! Merge with this one. */

			   /* Update the header with the new size from "us" */
			   left_area_header->size += header->size;
			   assert(FOOTER_FROM_HEADER(left_area_header) == footer);

			   /* Since we are to the right, use our footer. Rewrite it to use the correct header, though: */
			   footer->header = left_area_header;

			   /* Re-point the header to the new area; this completes the merge */
			   header = left_area_header;

			   /* This area should not be added to the index, because "we" are now the area we found to the left,
				* and THAT should already be in the index. */
			   add_to_index = false;
		   }
	}

	/* TODO: unify right */
	/* Make sure to not access past heap->end_address */

	if (add_to_index)
		insert_ordered_array((void *)header, &kheap->free_index);


	/* TODO: Contract the heap if the last free area is (some amount - at least a few megs!), and the heap is over it's min. size */

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

	/* Calculate the end address */
	uint32 end_address = start_address + initial_size;
	assert(IS_PAGE_ALIGNED(end_address));

	/* Set up the structure */
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
		void *addr = heap_alloc(size, align, kheap);
		if (phys != 0) {
			page_t *page = get_page((uint32)addr, 0, kernel_directory);
			*phys = (page->frame * 0x1000) + ((uint32)addr & 0xfff);
		}
		return addr;
	}
	else {
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
	}
}

void kfree(void *p) {
	heap_free(p, kheap);
}

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
