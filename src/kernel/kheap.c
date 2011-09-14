#include <types.h>
#include <kernel/kheap.h>
#include <kernel/kernutil.h>
#include <kernel/paging.h>
#include <kernel/monitor.h> /* for print_heap_index() */
#include <stdio.h> /* sprintf */
#include <string.h> /* memset */

/* Calculates a footer location, given a header, since this is done over and over. */
#define FOOTER_FROM_HEADER(___he,___size) ((footer_t *)( (uint32)___he + ___size - sizeof(footer_t) ))

/* The kernel heap */

heap_t *kheap = NULL;

/* paging.c */
extern page_directory_t *kernel_directory;

static sint32 find_smallest_hole(uint32 size, uint8 page_align, heap_t *heap) {
	sint32 i = 0;

	 for (i = 0; i < heap->index.size; i++) {
		header_t *header = (header_t *)lookup_ordered_array(i, &heap->index);
		assert(header != NULL);

		if (! header->is_hole)
			continue;

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
	}
	
	/* Why did the loop exit? */
	if (i == heap->index.size)
		return -1; /* We got to the end and didn't find anything. */
	else
		return i;
}

/* Creates a block - or hole - in memory. It is NOT added to the heap map! */
/* Returns a header_t pointer to the new block. */
static header_t *create_block(uint32 address, uint32 size, uint8 is_hole) {
	assert (kheap != NULL);

	header_t *header_to_create = (header_t *)address;
//	footer_t *footer = (footer_t *) ((uint32)header + size - sizeof(footer_t));
	footer_t *footer_to_create = FOOTER_FROM_HEADER(header_to_create, size);
	/* A couple of sanity checks... */
	assert(address + size <= kheap->end_address);
	assert((uint32)footer_to_create + sizeof(footer_t) <= kheap->end_address); /* TODO: is this exactly the same as above? */

	/* Expensive sanity check: no existing address must be in this address space! */
	for (int iterator = 0; iterator < kheap->index.size; iterator++) {
		header_t *found_header = lookup_ordered_array(iterator, &kheap->index);
		footer_t *found_footer = FOOTER_FROM_HEADER(found_header, found_header->size);

		assert(found_header->magic == HEAP_MAGIC);
		assert(found_footer->magic == HEAP_MAGIC);
		assert(found_footer->header == found_header);

		/* Obvious? Yes. Unlikely? Yes. Worth it, at this stage? Yes. */
		assert(found_header != header_to_create);
		assert(found_footer != footer_to_create);
		assert((uint32)found_header != (uint32)footer_to_create);
		assert((uint32)found_footer != (uint32)header_to_create);

		if (found_header > header_to_create) {
			/* The header we found is more to the right than us */
			assert( (uint32)header_to_create + size <= (uint32)found_header );
		}
		else if (found_header < header_to_create) {
			/* We are further to the right than the header we found */
			assert( (uint32)found_header + found_header->size <= (uint32)header_to_create );
		}
		else {
			panic("This should never be reached (since this case is taken care of above)!");
		}
	}

	header_to_create->magic = HEAP_MAGIC;
	header_to_create->size = size;
	header_to_create->is_hole = is_hole;

	footer_to_create->magic = HEAP_MAGIC;
	footer_to_create->header = header_to_create;

	return header_to_create;
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
	memset(heap->index.array, 0, HEAP_INDEX_SIZE * sizeof(type_t));

	/* Since we can't place data in the index array, move the start address forward */
	start += sizeof(type_t) * HEAP_INDEX_SIZE;

	/* Is it still page aligned? If not, align it! */
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
	//header_t *header = create_block(start, end_addr - start, 1);

	/* This is the only instance where create_block should NOT be used, as it relies on kheap != NULL. */
	header_t *initial_hole = (header_t *)start;
	initial_hole->size = end_addr - start;
	initial_hole->magic = HEAP_MAGIC;
	initial_hole->is_hole = 1;

	footer_t *initial_footer = FOOTER_FROM_HEADER(initial_hole, end_addr - start);
	assert ((uint32)initial_footer + sizeof(footer_t) <= heap->end_address);
	initial_footer->magic = HEAP_MAGIC;
	initial_footer->header = initial_hole;

	insert_ordered_array((void *)initial_hole, &heap->index);

	return heap;
}

void print_heap_index(void) {
	/* A debug function that prints the heap map, that is, shows the blocks and holes. */

	if (kheap == NULL) {
		printk("print_heap_index() called with a NULL kheap! Returning early.\n");
		return;
	}

	int i = 0;
	printk("------\n");
	while (i < kheap->index.size) {
		header_t *header = (header_t *)lookup_ordered_array(i, &kheap->index);
		if (header != NULL) {
			//footer_t *footer = (footer_t *)( (uint32)header + header->size - sizeof(footer_t) );
			footer_t *footer = FOOTER_FROM_HEADER(header, header->size);

			printk("%s: %p to %p (%d bytes); %s header magic, %s footer magic\n",
					(header->is_hole ? "hole" : "block"),
					header,
					(uint32)header + header->size,
					header->size,
					(header->magic == HEAP_MAGIC ? "valid" : "INVALID"),
					(footer->magic == HEAP_MAGIC ? "valid" : "INVALID"));
#include <kernel/timer.h>
			if (header->magic != HEAP_MAGIC || footer->magic != HEAP_MAGIC)
				sleep(2000);
			if (footer->header != header)
				sleep(2000);

			i++;
		}
	}
	printk("------\n");
}

void validate_heap_index(void) {
	if (kheap == NULL) {
		return;
	}

	int i = 0;
	while (i < kheap->index.size) {
		header_t *header = (header_t *)lookup_ordered_array(i, &kheap->index);
		if (header != NULL) {
//			footer_t *footer = (footer_t *)( (uint32)header + header->size - sizeof(footer_t) );
			footer_t *footer = FOOTER_FROM_HEADER(header, header->size);

			if (header->magic != HEAP_MAGIC || footer->magic != HEAP_MAGIC) {
				char buf[128];
				sprintf(buf, "validate_heap_index: invalid magic for header at 0x%p! header magic=0x%x, footer magic=0x%x\n", header, header->magic, footer->magic);
				panic(buf);
			}
			
			if (footer->header != header) {
				char buf[128];
				sprintf(buf, "validate_heap_index: /header/ pointer in footer structure for %s with header at %p is invalid! footer->header == %p", 
						(header->is_hole ? "hole" : "block"),
						header, footer->header);
				panic(buf);
			}

			/* I would validate the size compared to (footer_addr - header_addr), but the footer is calculated pretty much that way! */

			i++;
		}
	}
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
	while (i < new_size + 0x1000) {
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
		uint32 idx = -1;
		uint32 value = 0;

		/* Loop through all the headers... */
		while (iterator < heap->index.size) {
			uint32 tmp = (uint32)lookup_ordered_array(iterator, &heap->index);
			if (tmp > value && ((header_t *)tmp)->is_hole == 1 ) {
				/* This *address* is larger than the previously highest one found */
				value = tmp;
				idx = iterator;
			}
			iterator++;
		}

		/* If we didn't find ANY headers, we need to add one. */
		if (idx == -1) {
			create_block(old_end_address, new_length - old_length, 1);
			/*
			header_t *header = (header_t *)old_end_address;
			header->magic = HEAP_MAGIC;
			header->size = new_length - old_length;
			header->is_hole = 1;
			footer_t *footer = (footer_t *) (old_end_address + header->size - sizeof(footer_t));
			footer->magic = HEAP_MAGIC;
			footer->header = header;
			*/

			insert_ordered_array((void *)old_end_address, &heap->index);
		}
		else {
			/* The last header needs adjusting */
			header_t *header = lookup_ordered_array(idx, &heap->index);
			header->size += new_length - old_length;
/*
			printk("end_address: %p\n", heap->end_address);
			printk("header at  : %p\n", (uint32)header);
			printk("footer at  : %p\n", ( (uint32)header + header->size - sizeof(footer_t) ));
*/

			if ((uint32)header + header->size > heap->end_address) {
				/* Uh oh! This must never happen! */
//				expand((heap->end_address - heap->start_address) + header->size, heap);
//				if ((uint32)header + header->size > heap->end_address) {
					/* Did that help? If not, give up and just use the smaller size: */
					header->size = (uint32)heap->end_address - (uint32)header;
//				}
			}

			assert((uint32)header + header->size <= heap->end_address);
			assert(addr_is_mapped((uint32)header));
			assert(addr_is_mapped((uint32)header + header->size));

			/* Rewrite the footer (since its location is now inside the now-resized data block) */
//			footer_t *footer = (footer_t *)( (uint32)header + header->size - sizeof(footer_t));
			footer_t *footer = FOOTER_FROM_HEADER(header, header->size);
			footer->header = header;
			footer->magic = HEAP_MAGIC;
		}

		/* We now have enough space. Recurse, and call the function again. */
		return alloc(size, page_align, heap);
	}
	/* end of if (iterator == -1) */

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
	if (page_align && (orig_hole_pos & 0xfffff000) != 0 &&
			 (0x1000 - (orig_hole_pos & 0xfff) - sizeof(header_t)) >= (8 + sizeof(header_t) + sizeof(footer_t))) {
		/* Go the next page boundary, and subtract the size of the header */
		uint32 new_location = orig_hole_pos + 0x1000 - (orig_hole_pos & 0xfff) - sizeof(header_t);

		create_block(orig_hole_pos, 0x1000 - (orig_hole_pos & 0xfff) - sizeof(header_t), 1);

		/* This is the header for the hole we found - so we can assign to it to change it, in-place. */
		/*
		header_t *hole_header = (header_t *)orig_hole_pos;
		hole_header->size     = 0x1000 - (orig_hole_pos & 0xfff) - sizeof(header_t);
		hole_header->magic    = HEAP_MAGIC;
		hole_header->is_hole  = 1;

		
		footer_t *hole_footer = (footer_t *) ( (uint32)new_location - sizeof(footer_t) );
		assert((uint32)new_location - sizeof(footer_t) == orig_hole_pos + hole_header->size - sizeof(footer_t));
		hole_footer->magic    = HEAP_MAGIC;
		hole_footer->header   = hole_header;
		*/

		orig_hole_pos         = new_location;
		orig_hole_size        = orig_hole_size - (0x1000 - (orig_hole_pos & 0xfff) - sizeof(header_t));
	}
	else if (0x1000 - (orig_hole_pos & 0xfff) - sizeof(header_t) <= (8 + sizeof(header_t) + sizeof(footer_t)) ) {
			/* Don't bother if the storage size is 8 bytes or less.
			 * Also fixes a super-annoying corruption issue that took days to fix.
			 */

			/* Remove from the index */
			int iterator = 0;
			while (iterator < heap->index.size && lookup_ordered_array(iterator, &heap->index) != (void *)orig_hole_pos)
				iterator++;
			if (iterator < heap->index.size) {
				remove_ordered_array(iterator, &heap->index);
			}
	}
	else {
		/* We don't need this hole any more, delete it from the index. */
		/* TODO: doesn't this simply delete a hole if it ALREADY IS page aligned?! */
		remove_ordered_array(iterator, &heap->index);
	}

	/* Set up the header and footer to indicate the parameters for this block */
	header_t *block_header = create_block(orig_hole_pos, new_size, 0);

	/* Add the block to the heap map */
	insert_ordered_array((void *)block_header, &heap->index);

	/*
	header_t *block_header = (header_t *)orig_hole_pos;
	footer_t *block_footer = (footer_t *) (orig_hole_pos + size + sizeof(header_t));
	assert (orig_hole_pos + size + sizeof(header_t) == (uint32)block_header + new_size - sizeof(footer_t) );

	block_header->magic    = HEAP_MAGIC;
	block_header->is_hole  = 0;
	block_header->size     = new_size;
	block_footer->magic    = HEAP_MAGIC;
	block_footer->header   = block_header;
	*/

	/* Create a new hole after the allocated block, if there's any space for it. */
	if (orig_hole_size - new_size > 0) {
//		header_t *hole_header = (header_t *) (orig_hole_pos + size + sizeof(header_t) + sizeof(footer_t));
		header_t *hole_header = create_block(
				orig_hole_pos + size + sizeof(header_t) + sizeof(footer_t), /* location */
				orig_hole_size - new_size, /* size */
				1); /* is a hole, not a block */

	/*
		hole_header->magic   = HEAP_MAGIC;
		hole_header->is_hole = 1;
		hole_header->size   = orig_hole_size - new_size;

		footer_t *hole_footer = (footer_t *) ( (uint32)hole_header + orig_hole_size - new_size - sizeof(footer_t) );

		if ((uint32)hole_footer + sizeof(footer_t) <= heap->end_address) {
			hole_footer->magic = HEAP_MAGIC;
			hole_footer->header = hole_header;
		}
	*/

		/* Save the new hole in the index */
		insert_ordered_array((void *)hole_header, &heap->index);
	}

	return (void *) ( (uint32)block_header + sizeof(header_t) );
}

void free(void *p, heap_t *heap) {
	if (p == NULL)
		return;

	header_t *header = (header_t *) ( (uint32)p - sizeof(header_t) );
//	footer_t *footer = (footer_t *) ( (uint32)header + header->size - sizeof(footer_t) );
	footer_t *footer = FOOTER_FROM_HEADER(header, header->size);

	assert(header->magic == HEAP_MAGIC);
	assert(footer->magic == HEAP_MAGIC);
	assert(footer->header == header);

	/* Mark this as a hole */
	header->is_hole = 1;

	/* Should we add this to the index of "free holes" later? */
	/* By default, we should NOT, any more, since blocks ARE in the index already! */
	bool add_to_index = false;

	/* Unify left if the thing immediately to the left of us is a footer... */
	footer_t *test_footer = (footer_t *)( (uint32)header - sizeof(footer_t) );
	if (test_footer->magic == HEAP_MAGIC && 
		test_footer->header->magic == HEAP_MAGIC &&
		test_footer->header->is_hole == 1)
	{
		/* With all the criteria above met, it appears extremely likely that there is a valid block to our left.
		 * Merge the two blocks, leaving the other block's header and our footer, though with corrected values. */
		uint32 cache_size = header->size; /* our current size */
		header_t *orig_header = header;
		header = test_footer->header; /* since we're to the right, use the other header */

		/* Update the heap map with the new header */
		/*
		int tmp = 0;
		for (tmp = 0; tmp < heap->index.size; tmp++) {
			if (lookup_ordered_array(tmp, &heap->index) == (type_t)orig_header) {
				if (indexof_ordered_array(header, &heap->index) == -1)
					update_ordered_array(tmp, header, &heap->index);
			}
		}
	*/

		header->size += cache_size;   /* update the header's size */
		footer->header = header;      /* rewrite our footer to use the new header */
		assert(FOOTER_FROM_HEADER(header, header->size) == footer);

		/* We're now merged with the hole to our left; REMOVE us from the index! */
		int tmp = 0;
		for (tmp = 0; tmp < heap->index.size; tmp++) {
			if (lookup_ordered_array(tmp, &heap->index) == (type_t)orig_header) {
				remove_ordered_array(tmp, &heap->index);
				break; // There should only be one hit anyway!
			}
		}
	}

	/* Unify right if the thing immediately to our right is a header... */
	header_t *test_header = (header_t *)( (uint32)footer + sizeof(footer_t) );
	if (test_header->magic == HEAP_MAGIC && test_header->is_hole == 1) 
	{
		/* Extra verification */
		test_footer = (footer_t *) ( (uint32)test_header + test_header->size - sizeof(footer_t) );
		test_footer = FOOTER_FROM_HEADER(test_header, test_header->size);


		if (test_footer->magic != HEAP_MAGIC || test_footer->header != test_header) {
			/* Looks like this wasn't valid after all! */
			panic("Invalid footer magic (when trying to perform right unification)!");
		}

		/* Grow this hole to include the other one */
		header->size += test_header->size;
		/* Replace the footer with the rightmost one (which is calculated and verified above */
		footer = test_footer;
		footer->header = header;
//		assert( ((uint32)header + header->size - sizeof(footer_t)) == (uint32)footer);
		assert( (uint32)FOOTER_FROM_HEADER(header, header->size) == (uint32)footer);

		/* Remove this header from the index */
		uint32 iterator = 0;
		while ( (iterator < heap->index.size) && (lookup_ordered_array(iterator, &heap->index) != (void *)test_header) )
			iterator++;

		/* Did we find it? If so, remove it. */
		if (iterator < heap->index.size)
			remove_ordered_array(iterator, &heap->index);

	}
	/* end unify right */

	/* If the footer location is the end address, we can contract the heap. */
	/* TODO: is it really a good idea to expand and contract the heap every time it's *possible*? */
	if ( (uint32)footer + sizeof(footer_t) == heap->end_address) {
		uint32 old_length = heap->end_address - heap->start_address;
		uint32 new_length = contract( (uint32)header - heap->start_address, heap);

		/* Check how big this hole will be after resizing the heap */
		if (header->size - (old_length - new_length) > 0) {
			/* This hole will still exist (i.e. contract() stopped a bit early), so resize it */
			header->size -= old_length - new_length;
//			footer = (footer_t *)( (uint32)header + header->size - sizeof(footer_t) );
			footer = FOOTER_FROM_HEADER(header, header->size);
			footer->magic = HEAP_MAGIC;
			footer->header = header;

			add_to_index = false;

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

			add_to_index = false;
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
