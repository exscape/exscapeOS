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

struct tss_entry 
{
   uint32 prev_tss;   // The previous TSS - if we used hardware task switching this would form a linked list.
   uint32 esp0;       // The stack pointer to load when we change to kernel mode.
   uint32 ss0;        // The stack segment to load when we change to kernel mode.
   uint32 esp1;       // Unused... (This entry an all uncommented ones)
   uint32 ss1;
   uint32 esp2;
   uint32 ss2;
   uint32 cr3;
   uint32 eip;
   uint32 eflags;
   uint32 eax;
   uint32 ecx;
   uint32 edx;
   uint32 ebx;
   uint32 esp;
   uint32 ebp;
   uint32 esi;
   uint32 edi;
   uint32 es;         // The value to load into ES when we change to kernel mode.
   uint32 cs;         // The value to load into CS when we change to kernel mode.
   uint32 ss;         // The value to load into SS when we change to kernel mode.
   uint32 ds;         // The value to load into DS when we change to kernel mode.
   uint32 fs;         // The value to load into FS when we change to kernel mode.
   uint32 gs;         // The value to load into GS when we change to kernel mode.
   uint32 ldt;        // Unused...
   uint16 trap;
   uint16 iomap_base;
} __attribute__((packed));

typedef struct tss_entry tss_entry_t;

void tss_switch(uint32 esp0, uint32 esp, uint32 ss);

#endif
