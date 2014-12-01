#include <string.h>
#include <kernel/heap.h>
#include <kernel/interrupts.h>
#include <kernel/kernutil.h>
#include <kernel/pmm.h>
#include <kernel/multiboot.h>
#include <kernel/console.h> // TODO: debugging only

/* The bitset of free/used frames */
uint32 *used_frames;
uint32 nframes;

// Store the last allocated frame address, and only bother searching for free
// frames prior to it we've freed a frame since. This is not optimal as-is, but simple.
uint32 last_allocated_frame;

uint32 mem_end_page;
extern uint32 placement_address;

/* Bitmap macros */
/* 32 == sizeof(uint32) in bits, so these simply calculate which dword a bit belongs to,
 * and the number of bits to shift that dword to find it, respectively. */
#define ARRAY_INDEX(a) (a >> 5) // divide by 32
#define OFFSET_INTO_DWORD(a)(a & 31) // mod 32

/* Set a bit in the used_frames bitmap */
void _pmm_set_frame(uint32 phys_addr) { // TODO: static!
	assert(interrupts_enabled() == false);
	uint32 frame_index = phys_addr / PAGE_SIZE;
	uint32 index = ARRAY_INDEX(frame_index);
	assert (index <= nframes/32);
	uint32 offset = OFFSET_INTO_DWORD(frame_index);
	assert((used_frames[index] & (1 << offset)) == 0);
	used_frames[index] |= (1 << offset);
}

/* Clear a bit in the used_frames bitmap */
static void _pmm_clear_frame(uint32 phys_addr) {
	assert(interrupts_enabled() == false);
	uint32 frame_index = phys_addr / PAGE_SIZE;
	uint32 index = ARRAY_INDEX(frame_index);
	assert (index <= nframes/32);
	uint32 offset = OFFSET_INTO_DWORD(frame_index);
	assert((used_frames[index] & (1 << offset)) != 0);
	used_frames[index] &= ~(1 << offset);
}

/* Test whether a bit is set in the used_frames bitmap */
static bool _pmm_test_frame(uint32 phys_addr) {
	assert(interrupts_enabled() == false);
	uint32 frame_index = phys_addr / PAGE_SIZE;
	uint32 index = ARRAY_INDEX(frame_index);
	assert (index <= nframes/32);
	uint32 offset = OFFSET_INTO_DWORD(frame_index);
	if ((used_frames[index] & (1 << offset)) != 0)
		return true;
	else
		return false;
}

void pmm_init(uint32 mbd_mmap_addr, uint32 mbd_mmap_length, uint32 upper_mem) {
	/* upper_mem is provided by GRUB; it's the number of *continuous* kilobytes of memory starting at 1MB (0x100000). */
	mem_end_page = 0x100000 + (uint32)upper_mem*1024;

	/* Ignore the last few bytes of RAM to align, if necessary */
	mem_end_page &= 0xfffff000;

	/*
	 * Check the GRUB memory map a first pass, to see if there are higher addresses
	 * than mem_end_page.
	 * This happens for me in VMware Fusion, but not in QEMU. With 256 MB RAM, QEMU
	 * gives two regions: a small one at below 1 MB, and then 0x100000 - 0x0FEF0000.
	 * VMWare fusion with 256 MB gives an area below 1 MB, 0x100000 - 0x0FEF0000,
	 * but ALSO 0x0FF00000 to 0x10000000.

	 *
	 * Because mem_end_page = 0x0FEF0000 (as it's based on the **continous** size),
	 * nframes is allocated too small, and we get a buffer overflow (or, rather,
	 * the assertions that prevent that fail, and we get a kernel panic).
	 *
	 * To solve this, we first check how high the largest physical address is, and
	 * then allocate based on that. Finally, we make a second pass through the map
	 * to actually set things up. This pass is only to find out how many frames
	 * there will be.
	 */
	if (mbd_mmap_addr != 0 && mbd_mmap_length != 0) {
		// We got a memory map from GRUB

		for (memory_map_t *memmap = (memory_map_t *)mbd_mmap_addr; (uint32)memmap < mbd_mmap_addr + mbd_mmap_length; memmap++) {
			if (memmap->type != 1) {
				continue;
			}

			if (memmap->base_addr_high != 0) {
				continue;
			}

			/* Page align addresses etc. */
			uint32 addr_lo = memmap->base_addr_low;
			if (addr_lo < PAGE_SIZE) // ignore the first page
				addr_lo = PAGE_SIZE;
			if (addr_lo & 0xfff) {
				addr_lo &= 0xfffff000;
				addr_lo += PAGE_SIZE;
			}

			uint32 addr_hi = addr_lo + (memmap->length_low);
			if (addr_hi & 0xfff)
				addr_hi &= 0xfffff000;

			if (addr_hi > mem_end_page) {
				mem_end_page = addr_hi;
			}
		}
	}

	/* The size of the bitmap is one bit per page */
	nframes = mem_end_page / PAGE_SIZE;

	/* allocate and initialize the bitmap */
	used_frames = (uint32 *)kmalloc((nframes / 32 + 1) * sizeof(uint32));

	// Set all frames to used, and clear the free areas below
	// (Reserved areas are set to "used", and never cleared, so they are always left alone.)
	memset(used_frames, 0xff, (nframes / 32 + 1) * sizeof(uint32));

	last_allocated_frame = 0xffffffff; // we can't use 0 since that's a valid frame

	INTERRUPT_LOCK;

	/*
	 * Utilize the GRUB memory map, if we got one.
	 * All frames are set to used (above), so that reserved areas
	 * are never used.
	 * Then, we loop through the frames in the free areas, and
	 * set them to free, so that they can be allocated by pmm_alloc*.
	 * We only use full pages/frames, so if there's memory available
	 * from 0x500 to 0x9f400, we use the range [0x1000, 0x9f000) and
	 * ignore the rest.
	 */
	if (mbd_mmap_addr != 0 && mbd_mmap_length != 0) {
		// We got a memory map from GRUB

		for (memory_map_t *memmap = (memory_map_t *)mbd_mmap_addr; (uint32)memmap < mbd_mmap_addr + mbd_mmap_length; memmap++) {
			if (memmap->type != 1) {
				// type == 1 means this area is free; all other types are reserved
				// and not available for use
				continue;
			}
#if 0
			printk("entry 0x%p: base 0x%p%p length 0x%p%p (free)\n", memmap,
				memmap->base_addr_high,
				memmap->base_addr_low,
				memmap->length_high,
				memmap->length_low);
#endif

			if (memmap->base_addr_high != 0) {
				printk("Warning: ignoring available RAM area above 4 GB\n");
				continue;
			}
			if (memmap->length_high != 0) {
				printk("Warning: ignoring part of available RAM (length > 4 GB)\n");
				// no continue, let's use the low 32 bits
			}

			/* Page align addresses etc. */
			uint32 addr_lo = memmap->base_addr_low;
			if (addr_lo < PAGE_SIZE) // ignore the first page
				addr_lo = PAGE_SIZE;
			if (addr_lo & 0xfff) {
				addr_lo &= 0xfffff000;
				addr_lo += PAGE_SIZE;
			}

			uint32 addr_hi = addr_lo + (memmap->length_low);

			if (memmap->base_addr_low < PAGE_SIZE) {
				// We adjusted the start of addr_lo above, without adjusting the length.
				// Since changing memmap-> seems like a bad idea, adjust addr_hi here instead.
				addr_hi -= PAGE_SIZE;
			}

			if (addr_hi & 0xfff)
				addr_hi &= 0xfffff000;

			if (addr_lo >= addr_hi) {
				// TODO: is this logic correct?
				// This is probably very unlikely, but not impossible; we used up this area in alignment
				continue;
			}

			// Make sure the alignment worked and won't cause us to
			// access memory outside the area
			assert(IS_PAGE_ALIGNED(addr_lo));
			assert(IS_PAGE_ALIGNED(addr_hi));
			assert(addr_lo >= memmap->base_addr_low);

			// Clear the addresses in this area
			uint32 addr;
			for (addr = addr_lo; addr < addr_hi; addr += PAGE_SIZE) {
				assert(addr < memmap->base_addr_low + memmap->length_low);
				_pmm_clear_frame(addr);
			}
		}
	}
	else {
		printk("Warning: no GRUB memory map found; ignoring/wasting all RAM below 1 MB\n");
		for (uint32 addr = 0x100000; addr < mem_end_page; addr += PAGE_SIZE) {
			// I would optimize this (memset() most of it), but even with 4 GB RAM,
			// QEMU does this in a lot less than a second... real computers are likely
			// faster.
			_pmm_clear_frame(addr);
		}
	}

	INTERRUPT_UNLOCK;
}

/* Returns the first free frame, roughly after (or at) /start_addr/ */
static uint32 _pmm_first_free_frame(uint32 start_addr) {
	assert(interrupts_enabled() == false);
	uint32 index = start_addr / PAGE_SIZE / 32;
	if (index != 0)
		index -= 1; // TODO: fix this - this is to be on the safe side by wasting time instead of getting bad results during the initial implementation phase

	for (; index < nframes / 32; index++) {
		if (used_frames[index] == 0xffffffff) {
			/* No bits are free among the 32 tested; try the next index */
			continue;
		}

		/* Since we're still here, at least one bit among these 32 is zero... Let's find the first. */
		// Offset starts at 0 which means we *may* return something earlier than start_addr,
		// but that is only indended as a rough guide, not a hard rule.
		for (uint32 offset = 0; offset < 32; offset++) {
			if ((used_frames[index] & (1 << offset)) == 0) {
				/* Found it! Return the frame address. */
				return (index * 32 + offset) * PAGE_SIZE;
			}
		}
	}

	/* If this is reached, there were no free frames! */
	return 0xffffffff;
}

void zero_page_physical(uint32 phys_addr);

uint32 pmm_alloc(void) {
	INTERRUPT_LOCK;
	if (last_allocated_frame == 0xffffffff)
		last_allocated_frame = 0;
	uint32 phys_addr = _pmm_first_free_frame(last_allocated_frame);
	if (phys_addr == 0xffffffff) {
		panic("pmm_alloc: no free frames (out of memory)!");
	}

	last_allocated_frame = phys_addr;

	_pmm_set_frame(phys_addr); // also tests that it's actually free

	zero_page_physical(phys_addr); // TODO: how much does this affect performance?

	INTERRUPT_UNLOCK;
	return phys_addr;
}

// Allocates /num_frames/ continuous physical frames
uint32 pmm_alloc_continuous(uint32 num_frames) {
	if (num_frames < 2)
		return pmm_alloc();

	INTERRUPT_LOCK;

	bool success = false;
	uint32 start = _pmm_first_free_frame(0);

	/*
	 * The idea behind this (naïve but relatively simple) algorithm is:
	 * 1) Find the first free frame
	 * 2) Are (frame+1), (frame+2), (frame+...), (frame + (num_frames - 1)) also free?
	 *	3) If yes: we're done, allocate them and return
	 *  4) If no: find the next free frame; start looking *after* the used one we found in step 2
	 */

	while (!success) {
		success = true; // if set when the for loop breaks, we're done
		if (start + (num_frames - 1) * PAGE_SIZE > mem_end_page)
			panic("pmm_alloc_continuous: no large enough continuous region found");

		for (uint32 i=1; i < num_frames; i++) { // we know that start + 0 is free, so start looking at 1
			if (_pmm_test_frame(start + (i * PAGE_SIZE)) != 0) {
				// We found a non-free frame! D'oh!
				// Start over at the next possibly free address.
				start = start + ((i+1) * PAGE_SIZE);
				success = false;
				break;
			}
		}
		// if the for loop didn't break because of finding a page, success == true and we'll exit
	}

	// Phew! /num_frames/ starting at (and including) /start/ ought to be free now.
	for(uint32 i=0; i < num_frames; i++) {
		_pmm_set_frame(start + i * PAGE_SIZE);
	}

	INTERRUPT_UNLOCK;

	return start;
}

void pmm_free(uint32 phys_addr) {
	INTERRUPT_LOCK;
	_pmm_clear_frame(phys_addr); // Also checks that it's currently set to being used
	last_allocated_frame = 0xffffffff;
	INTERRUPT_UNLOCK;
}

/* Returns the amount of *physical* RAM that it still unused, i.e. unused_frame_count * 4096
 * Note that this function is simple, not fast! It should NOT be called often, e.g. in loops! */
uint32 pmm_bytes_free(void) {
	uint32 unused = 0;
	INTERRUPT_LOCK;

	for (uint32 index = 0; index < nframes/32; index++) {
		if (used_frames[index] == 0) {
			/* All 32 frames in this bitmap chunk are free */
			unused += PAGE_SIZE * 32;
			continue;
		}
		else if (used_frames[index] == 0xffffffff) {
			/* All 32 frames in this bitmap chunk are used */
			continue;
		}

		/* We're somewhere in between all used and all free; let's check a bit closer */
		for (uint32 offset = 0; offset < 32; offset++) {
			if ( (used_frames[index] & (1 << offset)) == 0 ) {
				unused += PAGE_SIZE;
			}
		}
	}

	INTERRUPT_UNLOCK;
	assert(unused % 4096 == 0);
	return unused;
}

uint32 pmm_bytes_used(void) {
	uint32 used = 0;
	INTERRUPT_LOCK;

	for (uint32 index = 0; index < nframes/32; index++) {
		if (used_frames[index] == 0) {
			/* All 32 frames in this bitmap chunk are free */
			continue;
		}
		else if (used_frames[index] == 0xffffffff) {
			/* All 32 frames in this bitmap chunk are used */
			used += 32*4096;
			continue;
		}

		/* We're somewhere in between all used and all free; let's check a bit closer */
		for (uint32 offset = 0; offset < 32; offset++) {
			if ( (used_frames[index] & (1 << offset)) != 0 ) {
				used += PAGE_SIZE;
			}
		}
	}

	INTERRUPT_UNLOCK;
	return used;
}
