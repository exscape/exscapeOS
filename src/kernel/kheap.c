#include <kernel/kheap.h>
#include <kernel/kernutil.h>
#include <kernel/paging.h>
#include <kernel/console.h> /* for print_heap_index() */
#include <stdio.h> /* sprintf */
#include <string.h> /* memset */

/* Enable extra debugging, i.e. much stricter checks for each alloc/free */
/* 0: no debugging
 * 1: more checks, but rarely text printed unless an error occurs
 * 2: even more checks, still little or no text
 * 3: same checks as 2, but more text printed */
/* DON'T SET THIS TO ZERO, EVER! It should remain at 2 unless we're benchmarking or whatever (why would anyone benchmark this?). */
#define HEAP_DEBUG 2
#if HEAP_DEBUG == 0
#error Seriously, HEAP_DEBUG == 0 is a bad idea
#endif

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
	uint32 total_index = 0;
	for (uint32 index_num=0; index_num < 2; index_num++) {
		/* The index we're working with right now */
		ordered_array_t *index = indexes[index_num];

		for (uint32 i=0; i < index->size; i++, total_index++) {
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
				printk("#%u: %s: %p to %p (%d bytes); %s pointer + magics\n",
						total_index + 1, /* used 1-based indexing for the "UI" */
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
void do_asserts_for_index(ordered_array_t *index, area_header_t *header_to_create, area_footer_t *footer_to_create, uint32 size) {

	/* First, make sure the paremeters make sense! */
	assert(index != NULL);
	assert(header_to_create != NULL);
	assert(footer_to_create != NULL);
	assert(size > sizeof(area_header_t) + sizeof(area_footer_t));

#if HEAP_DEBUG >= 2
	/* Loop through the entire index */
	for (uint32 i = 0; i < index->size; i++) {
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
#endif
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
	do_asserts_for_index(&kheap->free_index, header_to_create, footer_to_create, size);
	do_asserts_for_index(&kheap->used_index, header_to_create, footer_to_create, size);
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
	for (uint32 i = 0; i < heap->free_index.size; i++) {
		header = lookup_ordered_array(i, &heap->free_index);

#if HEAP_DEBUG >= 1
	/* More checks never hurt! Unless you count performance, of course... */
		assert(header->magic == HEAP_MAGIC);
		assert(header->type == AREA_FREE);
		assert(FOOTER_FROM_HEADER(header)->magic == HEAP_MAGIC);
		assert(FOOTER_FROM_HEADER(header)->header == header);
#endif

		if (header->size >= size) {
			/* We found something! */
			if (page_align) {
				/* The hole needs to be page aligned. We don't page align it here, but we need to 
				 * make sure that this hole is big enough even when you subtract the space lost
				 * to alignment. */
				uint32 location = (uint32)header;
				uint32 offset = 0;
				if (!IS_PAGE_ALIGNED(location + sizeof(area_header_t))) {
					/* We want to page-align the data, not the header! */
					offset = PAGE_SIZE - (location + sizeof(area_header_t)) % PAGE_SIZE;
				}
				/* If we don't use signed variables here, they underflow and cause big problems. */
				if ( (sint32)header->size - (sint32)offset >= (sint32)size) {
					/* This area is large enough despite the page alignment! */
					return header;
				}
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
	if ((size_to_add % PAGE_SIZE) != 0) {
		size_to_add &= 0xfffff000;
		size_to_add += PAGE_SIZE;
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

#if HEAP_DEBUG >= 3
	printk("heap_expand, adding %d bytes\n", size_to_add);
#endif

	/* Make sure the new end address is page aligned. If not, move it BACK to a page boundary. */
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
	/* start at the old end_address, and move forwards one page at a time */
	uint32 addr = heap->end_address; 
	while (addr < new_end_address + PAGE_SIZE /* allocate one extra page */) {
		assert(IS_PAGE_ALIGNED(addr));

		assert(heap->supervisor == 1); /* only until user mode is implemented, of course! */

		alloc_frame(addr, kernel_directory, (heap->supervisor ? PAGE_KERNEL : PAGE_USER), (heap->readonly ? PAGE_READONLY : PAGE_WRITABLE) );
		addr += PAGE_SIZE;
	}

	/* ... and, now that we have the space, expand the heap! */
	heap->end_address = new_end_address;
}

/* Contract the heap, freeing the pages and physical frames that are no longer used */
void heap_contract(uint32 bytes_to_shrink, heap_t *heap) {
	/* Don't bother shrinking less than 512 kiB; this isn't an OS for 386-based computers */
	if (bytes_to_shrink < 512*1024)
		return;

	/* Calculate the sizes */
	uint32 old_size = heap->end_address - heap->start_address;
	uint32 new_size = old_size - bytes_to_shrink;

	/* Don't shrink below the initial size. */
	if (new_size < KHEAP_INITIAL_SIZE && heap == kheap)
		new_size = KHEAP_INITIAL_SIZE;

	/* Calculate the new end address, and make sure it's page aligned */
	uint32 new_end_address = heap->start_address + new_size;
	if (!IS_PAGE_ALIGNED(new_end_address)) {
		new_end_address &= 0xfffff000;
		new_end_address += PAGE_SIZE;
	}

#if HEAP_DEBUG >= 3
	printk("heap_contract, removing %d bytes\n", (old_size - new_size) );
#endif

	/* Make sure the address is still aligned */
	assert(IS_PAGE_ALIGNED(new_end_address));

	/* Free the frames that make up the new-freed space */
	for (uint32 addr = (uint32)heap->end_address; addr > new_end_address; addr -= PAGE_SIZE) {
		free_frame(addr, kernel_directory);
	}

	heap->end_address = new_end_address;
}

void *heap_alloc(uint32 size, bool page_align, heap_t *heap) {
	/* Take the header and footer overhead into account! */
	size += sizeof(area_header_t) + sizeof(area_footer_t);

	area_header_t *area = find_smallest_hole(size, page_align, heap);

	if (area == NULL) {
		/* There were no free areas big enough! Expand the heap, and create one. */
		/* heap_expand expands with at least HEAP_MIN_GROWTH bytes, so we don't need to bother checking here */
		uint32 old_heap_size = heap->end_address - heap->start_address;
		heap_expand(size, heap);
		uint32 new_heap_size = heap->end_address - heap->start_address;
		assert(new_heap_size >= old_heap_size + HEAP_MIN_GROWTH);

		/*
		 * OK, so the heap is now expanded. However, that's not enough; we need a free area that's big enough!
		 * heap_expand() doesn't modify the areas - WE need to do so.
		 * First, we try to find the rightmost header (not the "rightmost free" - if there is a used area further to the right,
		 * we can't expand the rightmost free area, now can we?), hope it's free, and if so, grow it.
		 * If we don't find such an area, create one.
		 */

		/* First, let's check the rightmost area... */
		area_header_t *rightmost_area = heap->rightmost_area;
		/*
		 * This is old code, which is obviously much slower than the new code above. Both were used,
		 * with an assert(), to make sure the pointer was always up-to-date.
		 * I'll leave this here (commented out) for a while, just in case I want to make sure again. 
		area_header_t *rightmost_area = NULL;
		for (int i = 0; i < heap->free_index.size; i++) {
			area_header_t *test_area = (area_header_t *)lookup_ordered_array(i, &heap->free_index);
			if (test_area > rightmost_area) {
				rightmost_area = test_area;
			}
		}
		for (int i = 0; i < heap->used_index.size; i++) {
			area_header_t *test_area = (area_header_t *)lookup_ordered_array(i, &heap->used_index);
			if (test_area > rightmost_area) {
				rightmost_area = test_area;
			}
		}
		*/

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

	/* Remove this area from the free_index */
	/* This must be done before the check to create a second free area (below), or that will fail,
	 * since the footer for the entire free area we're using will have the same footer location as the new free area.
	 * An assert() checks that to prevent errors, and will fail if we try to create that hole before we do this.
	 * This must also be done before the if (page_align) clause below, since that part will actually modify the /area/ variable.
	 */
	remove_ordered_array_item((void *)area, &kheap->free_index);

	if (page_align && !IS_PAGE_ALIGNED((uint32)area + sizeof(area_header_t))) {
		/* The caller has requested the memory be page-aligned; sure, can do! */
		uint32 offset = 0;
		if (!IS_PAGE_ALIGNED((uint32)area + sizeof(area_header_t))) {
			/* We want to page-align the data, not the header! */
			offset = PAGE_SIZE - ((uint32)area + sizeof(area_header_t)) % PAGE_SIZE;
		}

		assert(area->size > offset); /* this assert is needed, or the one below may fail due to underflow! */
		assert( (uint32)area->size - offset >= size);

		/* /area/ will be overwritten by the hole we create in the wasted space due to alignment, so we need to save it. */
		area_header_t *orig_area = area;
		uint32 cached_size = area->size;

		if (offset > sizeof(area_header_t) + sizeof(area_footer_t)) {
			/* Create a free area in the otherwise wasted space between /area/ and /area + offset/ that we're going to use now */
			create_area((uint32)area, offset /* size */, AREA_FREE, heap);
			/* Add the "waste area" to the index */
			insert_ordered_array((void *)area, &heap->free_index);
		}
		else {
			/* This is rare, but happens... If we get here, the area between the previous area and the page-aligned area we want to create
			 * is larger than 0, but smaller than the overhead of an additional area. In other words, we can't write a header + footer
			 * here... 
			 * What we do is try to locate the area to the left of the wasted space, and increase it in size... */
			area_footer_t *test_footer = (area_footer_t *)( (uint32)area - sizeof(area_footer_t) );
			if (test_footer->magic == HEAP_MAGIC) {
				/* Looks like we found another area. */
				area_header_t *test_header = test_footer->header;
				if (test_header->magic == HEAP_MAGIC) {
					/* Yep, we found another area! Increase it in size, and create a new footer for it. */
					test_footer->magic = 0; /* invalidate the old footer */
					test_header->size += offset;

					/* Rewrite that area's footer, now that the size has increased */
					test_footer = FOOTER_FROM_HEADER(test_header);
					test_footer->magic = HEAP_MAGIC;
					test_footer->header = test_header;
				}
			}
		}

		/* Now that we've created that hole, start work on the one actually requested */
		/* re-point the area variable to the actual area we want to create */
		area = (area_header_t *)( (uint32)orig_area + offset );

		/* create_area for the page-aligned area (that was requested) is done below, like with every other area,
		 * but we need to write the size out, since the stuff below depends on it! */
		area->size = cached_size - offset;

		/* Make sure the area is aligned now, and that it's still big enough to fit the requested amount of bytes. */
		assert(IS_PAGE_ALIGNED((uint32)area + sizeof(area_header_t)));
		assert(area->size >= size);
	}

	/* Is there enough space in the free area we're using to fit us AND another free area? */
	if (area->size - size >= (16 + sizeof(area_header_t) + sizeof(area_footer_t))) {
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

	/* Don't try to free memory that is clearly not from the heap.
	 * Note that max_address is NOT the current highest address (eas determined by heap size),
	 * but rather the highest allowed address for the heap (around 0xcfffffff for the kernel heap).
	 */
	if ( (uint32)p > heap->max_address || (uint32)p < heap->start_address )
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

		assert(rightmost_footer == FOOTER_FROM_HEADER(rightmost_area));

		assert(rightmost_footer->header == rightmost_area);

		/* Shrink the heap by the amount that would cause the rightmost area to end up 1 MiB in size. */
		uint32 bytes_to_shrink = rightmost_area->size - 0x100000;

		/* Perform the actual contraction */
		uint32 old_heap_size = heap->end_address - heap->start_address;
		heap_contract(bytes_to_shrink, heap);
		uint32 new_heap_size = heap->end_address - heap->start_address;

		/* How much did we ACTUALLY shrink? */
		/* There's no guarantee that the heap is ANY smaller, because heap_contract refuses to shrink tiny amounts, since it's a waste of time. */
		uint32 bytes_shrunk = old_heap_size - new_heap_size;
		if (bytes_shrunk > 0) {
			/* Resize the area, now that the old footer should be outside the heap */
			rightmost_area->size -= bytes_shrunk;

			/* Write a new footer */
			rightmost_footer = FOOTER_FROM_HEADER(rightmost_area);
			rightmost_footer->magic = HEAP_MAGIC;
			rightmost_footer->header = rightmost_area;
		}

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
		start_address += PAGE_SIZE;
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
 * Do note that there is no kfree() until the kernel heap has been set up.
 */
void *kmalloc_int(uint32 size, bool align, uint32 *phys) {
	if (kheap != NULL) {
		void *addr = heap_alloc(size, align, kheap);
		if (phys != 0) {
			page_t *page = get_page((uint32)addr, true, kernel_directory);
			*phys = (page->frame * PAGE_SIZE) + ((uint32)addr & 0xfff);
		}
		if (align)
			assert(IS_PAGE_ALIGNED(addr));
		return addr;
	}
	else {
		/* kheap == NULL, i.e. we haven't created the kernel heap yet */

		if (align == true && !IS_PAGE_ALIGNED(placement_address)) {
			/* The page isn't page aligned; align it */
			placement_address &= 0xfffff000; /* align it to a page boundary */
			placement_address += PAGE_SIZE;     /* ... the NEXT page boundary */
		}

		if (phys != 0) {
			/* The caller wants to know the physical placement address. */
			*phys = placement_address;
		}

		/* Save the address */
		uint32 ret = placement_address;

		/* Move the pointer, for the next call */
		placement_address += size;

		if (align)
			assert(IS_PAGE_ALIGNED(ret));

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
