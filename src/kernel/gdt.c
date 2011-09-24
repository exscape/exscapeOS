#include <types.h>
#include <kernel/gdt.h>
#include <string.h> /* memset */
#include <kernel/kernutil.h> /* assert */

/* Create three kernel-global instances of GDT entries, and a pointer */
#define NUM_GDT_ENTRIES 6
struct gdt_entry gdt[NUM_GDT_ENTRIES];
struct gdt_ptr gp;

/* assembly helper function */
extern void tss_flush(void);

/* only used in this file */
static void write_tss(sint32, uint16, uint32);

tss_entry_t tss_entry;

/* Set up a GDT descriptor */
void gdt_set_gate(sint32 num, uint32 base, uint32 limit, uint8 access, uint8 gran) {
	assert(num <= NUM_GDT_ENTRIES - 1);

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
	gp.limit = (sizeof(struct gdt_entry)  * 6) - 1;
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

	/* Set up equivalent descriptors (first code, then data) for user mode */
	gdt_set_gate(3, 0, 0xffffffff, 0xfa, 0xcf);
	gdt_set_gate(4, 0, 0xffffffff, 0xf2, 0xcf);

	/* Set up our one TSS */
	write_tss(5, 0x10, 0);

	/* Install the new GDT! */
	gdt_flush();
	tss_flush();
}

static void write_tss(sint32 num, uint16 ss0, uint32 esp0) {
	/* Calculate what we need for the GDT entry */
	uint32 base = (uint32)&tss_entry;
	//uint32 limit = base + sizeof(tss_entry_t); /* TODO: FIXME: should this really be base plus... or just the size? */
	uint32 limit = sizeof(tss_entry_t);

	gdt_set_gate(num, base, limit, 0xe9, 0x00);

	memset(&tss_entry, 0, sizeof(tss_entry_t));

	tss_entry.ss0 = ss0;
	tss_entry.esp0 = esp0;
	tss_entry.iomap_base = sizeof(tss_entry_t);

	// Here we set the cs, ss, ds, es, fs and gs entries in the TSS. These specify what
	// segments should be loaded when the processor switches to kernel mode. Therefore
	// they are just our normal kernel code/data segments - 0x08 and 0x10 respectively,
	// but with the last two bits set, making 0x0b and 0x13. The setting of these bits
	// sets the RPL (requested privilege level) to 3, meaning that this TSS can be used
	// to switch to kernel mode from ring 3.
	tss_entry.cs = 0x0b;
	tss_entry.ss = tss_entry.ds = tss_entry.es = tss_entry.fs = tss_entry.gs = 0x13;
}

void set_kernel_stack(uint32 stack) {
	tss_entry.esp0 = stack;
}
