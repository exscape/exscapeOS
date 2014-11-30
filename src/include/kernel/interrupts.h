#ifndef _INTERRUPTS_H
#define _INTERRUPTS_H

#include <sys/types.h>

#define EXCEPTION_DIVISION_BY_ZERO 0
#define EXCEPTION_DEBUG 1 /* Reserved in modern CPUs */
#define EXCEPTION_NMI_INTERRUPT 2
#define EXCEPTION_BREAKPOINT 3
#define EXCEPTION_OVERFLOW 4
#define EXCEPTION_OUT_OF_BOUNDS 5
#define EXCEPTION_INVALID_OPCODE 6
#define EXCEPTION_NO_COPROCESSOR 7
#define EXCEPTION_DOUBLE_FAULT 8
#define EXCEPTION_COPROCESSOR_SEGMENT_OVERRUN 9 /* not used by modern CPUs */
#define EXCEPTION_BAD_TSS 10
#define EXCEPTION_SEGMENT_NOT_PRESENT 11
#define EXCEPTION_STACK_FAULT 12
#define EXCEPTION_GPF 13
#define EXCEPTION_PAGE_FAULT 14
#define EXCEPTION_UNKNOWN_INTERRUPT 15
#define EXCEPTION_X86_FPU_ERROR 16 /* aka Math Fault */
#define EXCEPTION_ALIGNMENT_CHECK 17
#define EXCEPTION_MACHINE_CHECK 18
#define EXCEPTION_SIMD_FP_EXCEPTION 19

#define INTERRUPT_LOCK bool reenable_interrupts = interrupts_enabled(); disable_interrupts()
#define INTERRUPT_UNLOCK if (reenable_interrupts) enable_interrupts()
#define YIELD asm volatile("int $0x7e")

void idt_install(void);

void disable_interrupts(void);
void enable_interrupts(void);
bool interrupts_enabled(void);

void dump_regs_and_bt(uint32 esp);

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

typedef struct
{
    uint32 gs, fs, es, ds;
    uint32 edi, esi, ebp, ebx, edx, ecx, eax;
    uint32 int_no, err_code;
    uint32 eip, cs, eflags, useresp, ss;
} __attribute__((packed)) registers_t;

/* Used to register callbacks for interrupts. */
typedef uint32 (*isr_t)(uint32);
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
