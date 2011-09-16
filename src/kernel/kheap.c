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

/* Does a bunch of sanity checks; expensive, but also priceless during development/debugging. */
void do_asserts_for_index(ordered_array_t *index, area_header_t *header_to_create, area_footer_t *footer_to_create, uint32 size, heap_t *heap) {

	/* First, make sure the paremeters make sense! */
	assert(index != NULL);
	assert(header_to_create != NULL);
	assert(footer_to_create != NULL);
	assert(size > sizeof(area_header_t) + sizeof(area_footer_t));

	/* Loop through the entire index */
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

		/* OK, so this is unlikely to get hit... but let's see */
		if (found_header > heap->rightmost_area) {
			/* TODO: remove either the panic or this entire if clause, based on whether it's ever hit */
			heap->rightmost_area = found_header;
			panic("do_asserts: found a header further to the right! Remove the panic() and KEEP the code path.");
		}

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

/* Called by various parts of alloc() (and possibly free()) to create new areas (free or used). */
static area_header_t *create_area(uint32 address, uint32 size, uint8 type, heap_t *heap) {
	/* Make sure that the parameters make sense */
	assert(heap != NULL);
	assert(type == AREA_USED || type == AREA_FREE);
	assert(size > sizeof(area_header_t) + sizeof(area_footer_t));
	assert(address >= heap->start_address && (address + size) <= heap->end_address);
	
	/* The caller of this function is responsible for details like page alignment.
	 * If we should page-align something, then address should BE page aligned by now. */

	area_header_t *header_to_create = (area_header_t *) address;
	area_footer_t *footer_to_create = (area_footer_t *) (address + size - sizeof(area_footer_t) );

#if HEAP_DEBUG
	/* Very expensive, but very useful sanity checks! */
	/* Due to the fact that we have TWO indexes, these checks are in a separate function. */
	do_asserts_for_index(&kheap->free_index, header_to_create, footer_to_create, size, heap);
	do_asserts_for_index(&kheap->used_index, header_to_create, footer_to_create, size, heap);
#endif

	/* Write the header and footer to memory */
	header_to_create->size = size;
	header_to_create->magic = HEAP_MAGIC;
	header_to_create->type = type;

	footer_to_create->magic = HEAP_MAGIC;
	footer_to_create->header = header_to_create;

	/* Keep track of the rightmost area */
	if (header_to_create > heap->rightmost_area)
		heap->rightmost_area = header_to_create;

	/* All done! */
	return header_to_create;
}


/* This function does what is says on the tin: loops through all the holes, and returns the first (=smallest) one that is still big enough. */
area_header_t *find_smallest_hole(uint32 size, bool page_align, heap_t *heap) {

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

/* Grows the heap, and allocates frames to store it on. */
void heap_expand(uint32 size_to_add, heap_t *heap) {
	/* Don't expand any less than HEAP_MIN_GROWTH bytes */
	if (size_to_add < HEAP_MIN_GROWTH)
		size_to_add = HEAP_MIN_GROWTH;

	/* "Page align" the size */
	if ((size_to_add % 0x1000) != 0) {
		size_to_add &= 0xfffff000;
		size_to_add += 0x1000;
	}

	/* Don't go past the maximum size */
	if (heap->start_address + size_to_add > heap->max_address) {
		/* If this happens, calculate the maximum size we can add without overreaching the boundary. */
		size_to_add = (heap->max_address - heap->start_address) /* maximum size, period */
			          - (heap->end_address - heap->start_address); /* current size */
	}

	/* Make sure the above worked properly */
	assert(heap->start_address + size_to_add <= heap->max_address);
	assert(heap->start_address + size_to_add > heap->start_address);

	uint32 new_end_address = heap->end_address + size_to_add;

	/* Make sure the new end address is page aligned. If not, move it BACK to a page boundary. */
	/* TODO: should this, as every other alignment function, move forward...? */
	if (!IS_PAGE_ALIGNED(new_end_address)) {
		new_end_address &= 0xfffff000;
		/* Calculate the new size_to_add */
		size_to_add -= (heap->end_address + size_to_add) - new_end_address;
	}

	/* Make sure, yet again, that we haven't screwed up */
	assert(IS_PAGE_ALIGNED(heap->start_address));
	assert(IS_PAGE_ALIGNED(new_end_address));
	assert(heap->end_address + size_to_add == new_end_address);
	assert(new_end_address > heap->end_address);

	/* Now, finally... Physically allocate the new frames */
	uint32 addr = heap->end_address; /* start at the old end_address */
	while (addr < new_end_address + 0x1000) {
		assert(IS_PAGE_ALIGNED(addr));
		alloc_frame( get_page(addr, true, kernel_directory), (heap->supervisor ? 1 : 0), (heap->readonly ? 0 : 1) );
		addr += 0x1000;
	}

	/* ... and, now that we have the space, expand the heap! */
	heap->end_address = new_end_address;
}

/* TEST FUNCTION, that is to be removed as soon as I feel certain that the pointer is updated whenever it needs to be */
void test_rightmost(void) {
		area_header_t *STORED_rightmost_area = kheap->rightmost_area;
		area_header_t *rightmost_area = NULL;
		for (int i = 0; i < kheap->free_index.size; i++) {
			area_header_t *test_area = (area_header_t *)lookup_ordered_array(i, &kheap->free_index);
			if (test_area > rightmost_area) {
				rightmost_area = test_area;
			}
		}
		/* We need to do the same for the USED index! */
		for (int i = 0; i < kheap->used_index.size; i++) {
			area_header_t *test_area = (area_header_t *)lookup_ordered_array(i, &kheap->used_index);
			if (test_area > rightmost_area) {
				rightmost_area = test_area;
			}
		}

		assert(STORED_rightmost_area == rightmost_area);
}

/* Contract the heap, freeing the pages and physical frames that are no longer used */
void heap_contract(uint32 bytes_to_shrink, heap_t *heap) {
	/* Don't bother shrinking less than 512 kiB; this isn't an OS for 386-based computers */
	assert(bytes_to_shrink > 512*1024); 

	/* Calculate the sizes */
	uint32 old_size = heap->end_address - heap->start_address;
	uint32 new_size = old_size - bytes_to_shrink;

	/* Calculate the new end address, and make sure it's page aligned */
	uint32 new_end_address = heap->start_address + new_size;
	if (!IS_PAGE_ALIGNED(new_end_address)) {
		new_end_address &= 0xfffff000;
		new_end_address += 0x1000;
	}

	/* Don't shrink below the initial size. */
	if (new_size < KHEAP_INITIAL_SIZE && heap == kheap)
		new_size = KHEAP_INITIAL_SIZE;

	/* Make sure the address is still aligned */
	new_end_address = heap->start_address + new_size;
	assert(IS_PAGE_ALIGNED(new_end_address));

	/* Free the frames that make up the new-freed space */
	for (uint32 addr = (uint32)heap->end_address; addr > new_end_address; addr -= 0x1000) {
		free_frame(get_page(addr, false, kernel_directory));
	}

	heap->end_address = new_end_address;
}

void *heap_alloc(uint32 size, bool page_align, heap_t *heap) {
	/* Take the header and footer overhead into account! */
	size += sizeof(area_header_t) + sizeof(area_footer_t);

	/* TODO: remove this when it works! */
	test_rightmost();

	area_header_t *area = find_smallest_hole(size, page_align, heap);

	if (area == NULL) {
		/* There were no free areas big enough! Expand the heap, and create one. */
		/* heap_expand expands with at least HEAP_MIN_GROWTH bytes, so we don't need to bother checking here */
		uint32 old_heap_size = heap->end_address - heap->start_address;
		heap_expand(size, heap);
		uint32 new_heap_size = heap->end_address - heap->start_address;
		/* TODO: we should probably require a certain amount here, not just any increase */
		assert(new_heap_size > old_heap_size);

		/*
		 * OK, so the heap is now expanded. However, that's not enough; we need a free area that's big enough!
		 * heap_expand() doesn't modify the areas - WE need to do so.
		 * First, we try to find the rightmost header (not the "rightmost free" - if there is a used area further to the right,
		 * we can't expand the rightmost free area, now can we?), hope it's free, and if so, grow it.
		 * If we don't find such an area, create one.
		 */

		/* First, let's check the rightmost area... */
		area_header_t *STORED_rightmost_area = heap->rightmost_area;
		area_header_t *rightmost_area = NULL;
		for (int i = 0; i < heap->free_index.size; i++) {
			area_header_t *test_area = (area_header_t *)lookup_ordered_array(i, &heap->free_index);
			if (test_area > rightmost_area) {
				rightmost_area = test_area;
			}
		}
		/* We need to do the same for the USED index! */
		for (int i = 0; i < heap->used_index.size; i++) {
			area_header_t *test_area = (area_header_t *)lookup_ordered_array(i, &heap->used_index);
			if (test_area > rightmost_area) {
				rightmost_area = test_area;
			}
		}

		/* TODO: When this always works, remove the loops above! 
		 * DO wait until *ALL* heap code is 100% done, though!!! */
		assert(STORED_rightmost_area == rightmost_area);

		/* rightmost_area now points to the rightmost area (free or not!) - or NULL. */

		if (rightmost_area != NULL && rightmost_area->type == AREA_FREE) {
			/* Add the space to this area */
			area_footer_t *rightmost_footer = FOOTER_FROM_HEADER(rightmost_area);
			
			/* "Delete" the old footer */
			rightmost_footer->magic = 0;

			/* Add the space difference between the new and old heap sizes */
			rightmost_area->size += (new_heap_size - old_heap_size);

			/* Write a new footer */
			rightmost_footer = FOOTER_FROM_HEADER(rightmost_area);
			rightmost_footer->magic = HEAP_MAGIC;
			rightmost_footer->header = rightmost_area;

			/* We're done! The hole was found in the index, so we don't need to add it. */
		}
		else {
			/* We didn't find anything useful! We need to add a new area. */
			if (rightmost_area == NULL) {
				panic("No areas whatsoever in the heap! This is a bug and should never happen.");
			}

			/* Now that the edge-case (which should never happen) is gone, we have a valid header in rightmost_header. */
			/* We need to add a new header to it's right. */

			area_header_t *new_header = (area_header_t *)( (uint32)rightmost_area + rightmost_area->size );
			create_area((uint32)new_header, new_heap_size - old_heap_size, AREA_FREE, heap);

			/* Since we created an area, we need to add it to the index. */
			insert_ordered_array((void *)new_header, &kheap->free_index);
		}

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

	/* TODO: remove this when it works! */
	test_rightmost();

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

	/* Should we add this hole to the index? This depends on whether we unify, and how (below). */
	bool add_to_index = true;

	/* 
	 * Here's how the index should work with the four possible cases:
  	 * 1) free() without any bordering free areas: add to index; thus this is the default (above)
	 * 2) free() + unify left (and left only): don't add to index (the left area is already there)
	 * 3) free() + unify right (and right only): *DO* add, and also remove the right area from the index
	 * 4) free() + unify both ways: DON'T add, or the area will end up a duplicate in the index.
	 */

	/* Check if the area to our left is another free area; if so, merge with it, aka. unify left */
	area_footer_t *left_area_footer = (area_footer_t *)( (uint32)header - sizeof(area_footer_t) );
	if ((uint32)left_area_footer >= (uint32)heap->start_address && /* make sure to not make an invalid access */
		left_area_footer->magic == HEAP_MAGIC) {
		/* Looks like we found another area! 
		 * Do some extra checks, and make sure it's a FREE area */
		   area_header_t *left_area_header = left_area_footer->header;
		   if (left_area_header->magic == HEAP_MAGIC && left_area_header->type == AREA_FREE) {
			   /* Yep! Merge with this one. */

			   /* Update the header with the new size from "us" */
			   left_area_header->size += header->size;
			   assert(FOOTER_FROM_HEADER(left_area_header) == footer);

			   /* Since we are to the right, use our footer. Rewrite it to use the correct header, though: */
			   footer->header = left_area_header;

			   /* Corrupt the old magic values - both "our" header and the "other" footer are now inside the new area!
			   * This is done to make sure they aren't used somewhere as if the areas still existed as before. */
			   header->magic = 0;
			   left_area_footer->magic = 0;

			   /* Since "our" area is disappearing, it will no longer be the rightmost, if it is at the moment. */
			   if (header == heap->rightmost_area)
				   heap->rightmost_area = left_area_header;

			   /* Re-point "our" header to the new area; this completes the merge */
			   header = left_area_header;

			   /* This area should not be added to the index, because "we" are now the area we found to the left,
				* and THAT should already be in the index. */
			   add_to_index = false;
		   }
	}

	/* Check if the area to our right is another free area; if so, merge with it, aka. unify right */
	area_header_t *right_area_header = (area_header_t *)( (uint32)footer + sizeof(area_footer_t) );
	if (right_area_header->magic == HEAP_MAGIC && right_area_header->type == AREA_FREE) {
		/* Looks like we found something! */
		area_footer_t *right_area_footer = FOOTER_FROM_HEADER(right_area_header);
		if (right_area_footer->magic == HEAP_MAGIC && right_area_footer->header == right_area_header) {
			/* Yep! Merge with this one. */
			
			/* Delete the rightmost hole from the index before we merge */
			remove_ordered_array_item((void *)right_area_header, &kheap->free_index);

			/* Add the newfound space to the leftmost header */
			header->size += right_area_header->size;
			
			/* Overwrite the old header and footer, just in case something tries to use them; 
			 * in that case, the asserts will catch the invalid magics */
			footer->magic = 0;
			right_area_header->magic = 0;

			/* Same as with the left unification above: if the header to our right is the rightmost of all areas, it won't be any more. */
			if (heap->rightmost_area == right_area_header)
				heap->rightmost_area = header;

			/* Merge the areas */
			footer = right_area_footer;
			assert(footer == FOOTER_FROM_HEADER(header));
			footer->header = header;
			header->type = AREA_FREE; /* just to be sure */

			if (add_to_index == true) {
				/* This could really be rewritten as "if (we_did_NOT_unify_left)", which is what we're really asking */
				/* If we did unify left, we should NOT add this to the index, or there will be a duplicate entry. */
				/* So, this statement does nothing... still. */
				add_to_index = true;
			}
		}
	}

	if (add_to_index)
		insert_ordered_array((void *)header, &kheap->free_index);

	/* Contract the heap, if there is enough space at the end that we can consider it a waste of physical frames */
	if (heap->rightmost_area->type == AREA_FREE && heap->rightmost_area->size >= HEAP_MAX_WASTE) {
		assert(HEAP_MAX_WASTE >= 0x100000); /* If it's less than 1 MiB, this won't work very well. */

		area_header_t *rightmost_area = heap->rightmost_area;
		area_footer_t *rightmost_footer = FOOTER_FROM_HEADER(rightmost_area);
		
		/* Sanity checks */
		assert(rightmost_area->magic == HEAP_MAGIC);
		assert(rightmost_footer->magic == HEAP_MAGIC);
		assert(rightmost_footer->header == header);

		/* Shrink the heap by the amount that would cause the rightmost area to end up 1 MiB in size. */
		uint32 bytes_to_shrink = rightmost_area->size - 0x100000;

		/* Perform the actual contraction */
		uint32 old_heap_size = heap->end_address - heap->start_address;
		heap_contract(bytes_to_shrink, heap);
		uint32 new_heap_size = heap->end_address - heap->start_address;

		/* How much did we ACTUALLY shrink? */
		uint32 bytes_shrunk = old_heap_size - new_heap_size;
		assert(bytes_shrunk > 0);

		/* Resize the area, now that the old footer should be outside the heap */
		rightmost_area->size -= bytes_shrunk;

		/* Write a new footer */
		rightmost_footer = FOOTER_FROM_HEADER(rightmost_area);
		rightmost_footer->magic = HEAP_MAGIC;
		rightmost_footer->header = rightmost_area;

		/* Since the header address hasn't changed, we don't need to modify the index. We're done! */
	}
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

	/* Add the area to the index */
	insert_ordered_array((void *)header_to_create, &heap->free_index);

	/* Keep track of the rightmost area - since this is the ONLY area, it's also the rightmost area! */
	heap->rightmost_area = header_to_create;
	
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
