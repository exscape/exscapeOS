#ifndef _GDTIDT_H
#define _GDTIDT_H

#include <types.h>

/* TODO: define all the exception numbers here */
#define EXCEPTION_PAGE_FAULT 14

void gdt_set_gate(int num, unsigned long base, unsigned long limit, unsigned char access, unsigned char gran);
void gdt_install(void);
void gdt_flush(void);

void idt_install(void);

void disable_interrupts(void);
void enable_interrupts(void);

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

typedef struct registers
{
   uint32 ds;
   uint32 edi, esi, ebp, esp, ebx, edx, ecx, eax;
   uint32 int_no, err_code;
   uint32 eip, cs, eflags, useresp, ss; // Pushed by the processor automatically.
} registers_t; 

/* Used to register callbacks for interrupts. */
typedef void (*isr_t)(registers_t);
void register_interrupt_handler(uint8 n, isr_t handler);

/* The mapping of IRQs to ISR handlers. */
#define IRQ0 32
#define IRQ1 33
#define IRQ2 34
#define IRQ3 35
#define IRQ4 36
#define IRQ5 37
#define IRQ6 38
#define IRQ7 39
#define IRQ8 40
#define IRQ9 41
#define IRQ10 42
#define IRQ11 43
#define IRQ12 44
#define IRQ13 45
#define IRQ14 46
#define IRQ15 47

#endif
