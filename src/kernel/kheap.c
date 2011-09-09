#include <types.h>
#include <kernel/kheap.h>
#include <kernel/kernutil.h>
#include <kernel/paging.h>

/* The kernel heap */
heap_t *kheap = NULL;

/* paging.c */
extern page_directory_t *kernel_directory;

static sint32 find_smallest_hole(uint32 size, uint8 page_align, heap_t *heap) {
	sint32 i = 0; /* FIXME: tutorial has uint, but since the return value is signed... */

	while (i < heap->index.size) {
		header_t *header = (header_t *)lookup_ordered_array(i, &heap->index);

		if (page_align != 0) {
			uint32 location = (uint32)header;
			sint32 offset = 0;

			/* calculate the offset required to have the block itself page-aligned,
			 * rather than the block header_t */
			if ( ((location + sizeof(header_t)) & 0xfffff000) != 0) 
					offset = 0x1000 - (location + sizeof(header_t)) % 0x1000;

			sint32 hole_size = (sint32)header->size - offset;

			if (hole_size >= (sint32)size)
				break;
		}
		else if (header->size >= size) {
			break;
		}

		i++;
	}
	
	/* Why did the loop exit? */
	if (i == heap->index.size)
		return -1; /* We got to the end and didn't find anything. */
	else
		return i;
}

static sint8 header_t_less_than(void *a, void*b) {
	return ( ((header_t *)a)->size < ((header_t *)b)->size ) ? 1 : 0;
}

heap_t *create_heap(uint32 start, uint32 end_addr, uint32 max, uint8 supervisor, uint8 readonly) {
	heap_t *heap = (heap_t *)kmalloc(sizeof(heap_t));

	assert(start % 0x1000 == 0);
	assert(end_addr % 0x1000 == 0);

	/* Create the index */
	heap->index = place_ordered_array((void *)start, HEAP_INDEX_SIZE, &header_t_less_than);

	/* Since we can't place data in the index array, move the start address forward */
	start += sizeof(type_t) * HEAP_INDEX_SIZE;

	/* Is it still page aligned? */
	if ((start & 0xfffff000) != 0) {
		start &= 0xfffff000;
		start += 0x1000;
	}

	/* Create the heap structure */
	heap->start_address = start;
	heap->end_address = end_addr;
	heap->max_address = max;
	heap->supervisor = supervisor;
	heap->readonly = readonly;

	/* Initialize the heap: to begin with, it's all one big hole. */
	header_t *initial_hole = (header_t *)start;
	initial_hole->size = end_addr - start;
	initial_hole->magic = HEAP_MAGIC;
	initial_hole->is_hole = 1;
	insert_ordered_array((void *)initial_hole, &heap->index);

	return heap;
}

/* Allow resizing of the heap, in case we run out of space. */
static void expand(uint32 new_size, heap_t *heap) {
	uint32 old_size = heap->end_address - heap->start_address;

	/* There's no point in expanding to a smaller size, is there? */
	assert(new_size > old_size);

	/* Page align */
	if ((new_size & 0xfffff000) != 0) {
		new_size &= 0xfffff000;
		new_size += 0x1000;
	}

	assert(heap->start_address + new_size <= heap->max_address);

	/* Physically allocate the new pages */
	uint32 i = old_size;
	while (i < new_size) {
		alloc_frame( get_page(heap->start_address + i, 1, kernel_directory), (heap->supervisor) ? 1 : 0, (heap->readonly) ? 0 : 1);
		i += 0x1000;
	}

	heap->end_address = heap->start_address + new_size;
}

static uint32 contract(uint32 new_size, heap_t *heap) {
   uint32 old_size = heap->end_address-heap->start_address;

   assert(new_size < old_size);
   
   /* Page align */
   if ((new_size & 0xfffff000) != 0) {
	   /* FIXME: the tutorial uses 0x1000 everywhere here, not 0xfffff000. I assume that's a typo! */
	   new_size &= 0xfffff000;
	   new_size += 0x1000;
   }

   if (new_size < HEAP_MIN_SIZE)
	   new_size = HEAP_MIN_SIZE;

   uint32 i = old_size - 0x1000;
   while (new_size < i) {
	   free_frame(get_page(heap->start_address + i, 0, kernel_directory));
	   i -= 0x1000;
   }

   heap->end_address = heap->start_address + new_size;

   return new_size;
}

/* Now, for the more involved functions... */

/* Allocates memory on the specified heap, taking care of unification etc. */
void *alloc(uint32 size, uint8 page_align, heap_t * const heap) {
	/* Take the header and footer size into account */
	uint32 new_size = size + sizeof(header_t) + sizeof(footer_t);

	sint32 iterator = find_smallest_hole(new_size, page_align, heap);

	if (iterator == -1) {
		 /* There was no hole large enough available, so we need to expand the heap. */

		/* Save some stuff */
		uint32 old_length = heap->end_address - heap->start_address;
		uint32 old_end_address = heap->end_address;

		/* We need to allocate more space for this heap. */
		/* TODO: is it really optimal to grow it this little (every future alloc is likely to need an expand())? */
		expand(old_length + new_size, heap);
		uint32 new_length = heap->end_address - heap->start_address;

		/* Find the last/rightmost header in the heap */
		iterator = 0;

		/* Vars to hold the index + value of the endmost header found so far. */
		/* TODO: think through this and comment better */
		uint32 idx = -1;
		uint32 value = 0;

		while (iterator < heap->index.size) {
			uint32 tmp = (uint32)lookup_ordered_array(iterator, &heap->index);
			if (tmp > value) {
				value = tmp;
				idx = iterator;
			}
			iterator++;
		}

		/* If we didn't find ANY headers, we need to add one. */
		if (idx == -1) {
			header_t *header = (header_t *)old_end_address;
			header->magic = HEAP_MAGIC;
			header->size = new_length - old_length;
			header->is_hole = 1;
			footer_t *footer = (footer_t *) (old_end_address + header->size - sizeof(footer_t));
			footer->magic = HEAP_MAGIC;
			footer->header = header;

			insert_ordered_array((void *)header, &heap->index);
		}
		else {
			/* The last header needs adjusting */
			header_t *header = lookup_ordered_array(idx, &heap->index);
			header->size += new_length - old_length;
			/* Rewrite the footer (since its location is now inside the now-resized data block) */
			footer_t *footer = (footer_t *)( (uint32)header + header->size - sizeof(footer_t));
			footer->header = header;
			footer->magic = HEAP_MAGIC;
		}

		/* We now have enough space. Recurse, and call the function again. */
		return alloc(size, page_align, heap);
	}

	header_t *orig_hole_header = (header_t *)lookup_ordered_array(iterator, &heap->index);
	uint32 orig_hole_pos = (uint32)orig_hole_header;
	uint32 orig_hole_size = orig_hole_header->size;
	
	/* Let's work out whether we should split this hole into two parts or not.
	 * Is the original hole size minus the requested hole size less than the overhead of a new hole? */
	if (orig_hole_size - new_size < sizeof(header_t) + sizeof(footer_t)) {
		/* Just increase the requested size to the size of the hole we found */
		size += orig_hole_size - new_size;
		new_size = orig_hole_size;
	}
	/* If we do need to split, that is done a bit further down. */

	/* If we need to page-align the data, do so, and make a new hole in front of this block. */
	if (page_align && (orig_hole_pos & 0xfffff000) != 0) {
		/* Go the next page boundary, and subtract the size of the header */
		uint32 new_location = orig_hole_pos + 0x1000 - (orig_hole_pos & 0xfff) - sizeof(header_t);
		/* This is the header for the hole we found - so we can assign to it to change it, in-place. */
		header_t *hole_header = (header_t *)orig_hole_pos;
		hole_header->size     = 0x1000 - (orig_hole_pos & 0xfff) - sizeof(header_t);
		hole_header->magic    = HEAP_MAGIC;
		hole_header->is_hole  = 1;
		
		footer_t *hole_footer = (footer_t *) ( (uint32)new_location - sizeof(footer_t) );
		hole_footer->magic    = HEAP_MAGIC;
		hole_footer->header   = hole_header;
		orig_hole_pos         = new_location;
		orig_hole_size        = orig_hole_size - hole_header->size;
	}
	else {
		/* We don't need this hole any more, delete it from the index. */
		/* TODO: doesn't this simply delete a hole if it ALREADY IS page aligned?! */
		remove_ordered_array(iterator, &heap->index);
	}

	/* Set up the header and footer to indicate the parameters for this block */
	header_t *block_header = (header_t *)orig_hole_pos;
	footer_t *block_footer = (footer_t *) (orig_hole_pos + size + sizeof(header_t));
	block_header->magic    = HEAP_MAGIC;
	block_header->is_hole  = 0;
	block_header->size     = new_size;
	block_footer->magic    = HEAP_MAGIC;
	block_footer->header   = block_header;

	/* Create a new hole after the allocated block, if there's any space for it. */
	if (orig_hole_size - new_size > 0) {
		header_t *hole_header = (header_t *) (orig_hole_pos + size + sizeof(header_t) + sizeof(footer_t));
		hole_header->magic   = HEAP_MAGIC;
		hole_header->is_hole = 1;
		hole_header->size   = orig_hole_size - new_size; /* TODO: does this take sizeof(header_t) + sizeof(footer_t) into account? */
		/* This is a mess... At this point I'm not completely sure what orig_hole_size and new_size refer to! */
		footer_t *hole_footer = (footer_t *) ( (uint32)hole_header + orig_hole_size - new_size - sizeof(footer_t) );

		if ((uint32)hole_footer + sizeof(footer_t) < heap->end_address) {
			/* TODO: I added the "+ sizeof(footer_t) above myself. Should work to prevent overwriting past the end of the heap. */
			hole_footer->magic = HEAP_MAGIC;
			hole_footer->header = hole_header;
		}

		/* Save the new hole in the index */
		insert_ordered_array((void *)hole_header, &heap->index);
	}

	return (void *) ( (uint32)block_header + sizeof(header_t) );
}

void free(void *p, heap_t *heap) {
	if (p == NULL)
		return;

	header_t *header = (header_t *) ( (uint32)p - sizeof(header_t) );
	footer_t *footer = (footer_t *) ( (uint32)header + header->size - sizeof(footer_t) );

	assert(header->magic == HEAP_MAGIC);
	assert(footer->magic == HEAP_MAGIC);

	/* Mark this as a hole */
	header->is_hole = 1;

	/* Should we add this to the index of "free holes" later? */
	bool add_to_index = true;

	/* Unify left if the thing immediately to the left of us is a footer... */
	footer_t *test_footer = (footer_t *)( (uint32)header - sizeof(footer_t) );
	if (test_footer->magic == HEAP_MAGIC && 
		test_footer->header->magic == HEAP_MAGIC &&
		test_footer->header->is_hole == 1)
	{
		/* With all the criteria above met, it appears extremely likely that there is a valid block to our left.
		 * Merge the two blocks, leaving the other block's header and our footer, though with corrected values. */
		uint32 cache_size = header->size; /* our current size */
		header = test_footer->header; /* since we're to the right, use the other header */
		footer->header = header;      /* rewrite our footer to use the new header */
		header->size += cache_size;   /* update the header's size */
		add_to_index = false;         /* since we're now merged with another hole, don't add us to the index! */
	}

	/* Unify right if the thing immediately to our right is a header... */
	header_t *test_header = (header_t *)( (uint32)footer + sizeof(footer_t) );
	if (test_header->magic == HEAP_MAGIC && test_header->is_hole == 1) 
	{
		/* Extra verification */
		test_footer = (footer_t *) ( (uint32)test_header + test_header->size - sizeof(footer_t) );
		if (test_footer->magic != HEAP_MAGIC || test_footer->header != test_header) {
			/* Looks like this wasn't valid after all! */
			panic("Found a valid header magic in right unification, but the footer was invalid. "
				  "Since this is unlikely, make sure the code is correct, especially if this happens more than once.");
			goto no_right_unify; /* ugghh; well, at least it's the only goto in ~2000 lines so far */
		}

		/* Grow this hole to include the other one */
		header->size += test_header->size;
		/* Replace the footer with the rightmost one (which is calculated and verified above */
		footer = test_footer;
		footer->header = test_header;

		/* Remove this header from the index */
		uint32 iterator = 0;
		while ( (iterator < heap->index.size) && (lookup_ordered_array(iterator, &heap->index) != (void *)test_header) )
			iterator++;

		/* Did we find it? If so, remove it. */
		if (iterator < heap->index.size)
			remove_ordered_array(iterator, &heap->index);

	}
	/* end unify right */

no_right_unify:

	/* If the footer location is the end address, we can contract the heap. */
	/* TODO: is it really a good idea to expand and contract the heap every time it's *possible*? */
	if ( (uint32)footer + sizeof(footer_t) == heap->end_address) {
		uint32 old_length = heap->end_address - heap->start_address;
		uint32 new_length = contract( (uint32)header - heap->start_address, heap);

		/* Check how big this hole will be after resizing the heap */
		if (header->size - (old_length - new_length) > 0) {
			/* This hole will still exist (i.e. contract() stopped a bit early), so resize it */
			header->size -= old_length - new_length;
			footer = (footer_t *)( (uint32)header + header->size - sizeof(footer_t) );
			footer->magic = HEAP_MAGIC;
			footer->header = header;

			/* TODO: clear the old magic value (in the old footer)? */
		}
		else {
			/* This hole will no longer exist, so remove it from the index. */
			uint32 iterator = 0;
			while ( (iterator < heap->index.size) &&
					(lookup_ordered_array(iterator, &heap->index) != (void *)test_header) )
			{
				iterator++;
			}

			/* If we didn't find anything, we don't have anything to remove. */
			if (iterator < heap->index.size)
				remove_ordered_array(iterator, &heap->index);
		}
	}
	/* end contract check */

	/* Add us to the index, if that's what we should do */
	if (add_to_index)
		insert_ordered_array((void *)header, &heap->index);

}









/* Defined in linker.ld */
extern uint32 end;

uint32 placement_address = (uint32)&end;

/* Allocate /size/ bytes of memory.
 * If align == true, the return value will be page aligned.
 * If phys isn't NULL, it will be set to the physical address of the allocated area.
 *
 * Do note that there is no kfree().
 */
uint32 kmalloc_int(uint32 size, bool align, uint32 *phys) {
	if (kheap != NULL) {
		void *addr = alloc(size, align, kheap);
		if (phys != 0) {
			page_t *page = get_page((uint32)addr, 0, kernel_directory);
			*phys = (page->frame * 0x1000) + ((uint32)addr & 0xfff);
		}
		return (uint32)addr;
	}
	else {
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
	}
}

void kfree(void *p) {
	free(p, kheap);
}

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
