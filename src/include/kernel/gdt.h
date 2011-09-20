#ifndef _GDT_H
#define _GDT_H

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

void gdt_set_gate(sint32 num, uint32 base, uint32 limit, uint8 access, uint8 gran);
void gdt_install(void);
void gdt_flush(void);

#endif
