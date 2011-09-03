#include <types.h>

/* A single GDT entry. Needs to be packed! */
struct gdt_entry {
	uint16 limit_low;
	uint16 base_low;
	uint8 base_middle;
	uint8 access;
	uint8 granularity;
	uint8 base_high;
} __attribute__((packed));

/* The 48-bit GDT pointer used by the LGDT instruction */
struct gdt_ptr {
	uint16 limit;
	uint32 base;
} __attribute__((packed));

/* Create three kernel-global instances of GDT entries, and a pointer */
struct gdt_entry gdt[3];
struct gdt_ptr gp;

/* Implemented in kernel.s */
extern void gdt_flush(void);

/* Set up a GDT descriptor */
void gdt_set_gate(sint32 num, uint32 base, uint32 limit, uint8 access, uint8 gran) {

	/* Set up the descriptor base address */
	gdt[num].base_low = (base & 0xFFFF);
	gdt[num].base_middle = (base >> 16) & 0xff;
	gdt[num].base_high = (base >> 24) & 0xff;

	/* Set up the descriptor limits */
	gdt[num].limit_low = (limit & 0xffff);
	gdt[num].granularity = ((limit >> 16) & 0x0f);

	/* Set up the granularity and access flags */
	gdt[num].granularity |= (gran & 0xf0);
	gdt[num].access = access;
}

/* 
 * This is the "main" GDT setup function, i.e. the one kmain() should call.
 * It sets up the GDT pointer, the first three entries, and then calls 
 * gdt_flush() to load the new GDT and update the segment registers. 
 */
void gdt_install(void) {
	/* Set up the GDT pointer */
	gp.limit = (sizeof(struct gdt_entry)  * 3) - 1;
	gp.base = (uint32)&gdt;

	/* Create the NULL descriptor */
	gdt_set_gate(0, 0, 0, 0, 0);

	/*
	 * Create the code segment descriptor.
	 * The base address is 0, limit is 4 GiB, 4 kiB granularity,
	 * uses 32-bit opcodes and is a CS descriptor.
	 */
	gdt_set_gate(1, 0, 0xffffffff, 0x9a, 0xcf);

	/*
	 * Create the data segment descriptor.
	 * The descriptor type is the only difference from
	 * the code segment descriptor above!
	 */
	gdt_set_gate(2, 0, 0xffffffff, 0x92, 0xcf);

	/* Install the new GDT! */
	gdt_flush();
}
