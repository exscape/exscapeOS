#include <types.h>
#include <string.h>

struct idt_entry {
	uint16 base_lo;
	uint16 sel;
	uint8 always0;
	uint8 flags;
	uint16 base_hi;
} __attribute__((packed));

struct idt_ptr {
	uint16 limit;
	uint32 base;
} __attribute__((packed));

/*
 * Declare an IDT of 256 entries. The first 32 are reserved for CPU exceptions,
 * the rest for other kinds of interrupts. Handling all interrupts
 * will also prevent unhandled interrupt exceptions.
 */
struct idt_entry idt[256];
struct idt_ptr idtp;

/* Implemented in kernel.s */
extern void idt_load(void);

/* Set an entry in the IDT */
void idt_set_gate(uint8 num, uint32 base, uint16 sel, uint8 flags) {
	/* We'll leave you to try and code this function: take the
    *  argument 'base' and split it up into a high and low 16-bits,
    *  storing them in idt[num].base_hi and base_lo. The rest of the
    *  fields that you must set in idt[num] are fairly self-
    *  explanatory when it comes to setup */
}

/* Installs the IDT; called from kmain() */
void idt_install(void) {
	/* Set the limit up - same theory here as with the GDT */
	idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
	idtp.base = (uint32)&idt;

	/* Zero the entire IDT; unused entries won't cause us any trouble */
	memset(&idt, 0, sizeof(struct idt_entry) * 256);

	/* Add ISRs */

	/* Load the new IDT */
	idt_load();
}


