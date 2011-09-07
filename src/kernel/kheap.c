#include <types.h>

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
	if (align == true && ((placement_address & 0xffffff000)) != 0) {
		/* The page isn't page aligned (the lower 12 bits aren't all-zero */
		placement_address &= 0xfffff000; /* align it to a page boundary */
		placement_address += 0x1000;     /* ... the NEXT page boundary */
	}

	if (phys != 0) {
		/* The caller wants to know the placement address. */
		*phys = placement_address;
	}

	/* Save the address */
	uint32 ret = placement_address;

	/* Move the pointer, for the next call */
	placement_address += size;

	return ret;
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
