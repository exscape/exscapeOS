#include <types.h>
#include <string.h>
#include <gdtidt.h>
#include <monitor.h>
#include <kernutil.h>

void disable_interrupts(void) {
	asm("cli");
}

void enable_interrupts(void) {
	asm("sti");
}

/* All of these are defined in kernel.s using NASM macros */
extern void isr0();
extern void isr1();
extern void isr2();
extern void isr3();
extern void isr4();
extern void isr5();
extern void isr6();
extern void isr7();
extern void isr8();
extern void isr9();
extern void isr10();
extern void isr11();
extern void isr12();
extern void isr13();
extern void isr14();
extern void isr15();
extern void isr16();
extern void isr17();
extern void isr18();
extern void isr19();
extern void isr20();
extern void isr21();
extern void isr22();
extern void isr23();
extern void isr24();
extern void isr25();
extern void isr26();
extern void isr27();
extern void isr28();
extern void isr29();
extern void isr30();
extern void isr31();
extern void irq0();
extern void irq1();
extern void irq2();
extern void irq3();
extern void irq4();
extern void irq5();
extern void irq6();
extern void irq7();
extern void irq8();
extern void irq9();
extern void irq10();
extern void irq11();
extern void irq12();
extern void irq13();
extern void irq14();
extern void irq15();

/* The array of ISR handlers */
isr_t interrupt_handlers[256] = {0};

void register_interrupt_handler(uint8 n, isr_t handler) {
	interrupt_handlers[n] = handler;
}

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
	idt[num].base_lo = (base & 0xffff);
	idt[num].base_hi = ((base >> 16) & 0xffff);
	idt[num].sel     = sel;
	idt[num].always0 = 0;
	idt[num].flags   = flags;
}

/* Installs the IDT; called from kmain() */
void idt_install(void) {
	/* Set the limit up - same theory here as with the GDT */
	idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
	idtp.base = (uint32)&idt;

	/* Zero the entire IDT; unused entries won't cause us any trouble */
	memset(&idt, 0, sizeof(struct idt_entry) * 256);

	/* Add ISRs */
	idt_set_gate(0, (uint32)isr0, 0x08, 0x8e);
	idt_set_gate(1, (uint32)isr1, 0x08, 0x8e);
	idt_set_gate(2, (uint32)isr2, 0x08, 0x8e);
	idt_set_gate(3, (uint32)isr3, 0x08, 0x8e);
	idt_set_gate(4, (uint32)isr4, 0x08, 0x8e);
	idt_set_gate(5, (uint32)isr5, 0x08, 0x8e);
	idt_set_gate(6, (uint32)isr6, 0x08, 0x8e);
	idt_set_gate(7, (uint32)isr7, 0x08, 0x8e);
	idt_set_gate(8, (uint32)isr8, 0x08, 0x8e);
	idt_set_gate(9, (uint32)isr9, 0x08, 0x8e);
	idt_set_gate(10, (uint32)isr10, 0x08, 0x8e);
	idt_set_gate(11, (uint32)isr11, 0x08, 0x8e);
	idt_set_gate(12, (uint32)isr12, 0x08, 0x8e);
	idt_set_gate(13, (uint32)isr13, 0x08, 0x8e);
	idt_set_gate(14, (uint32)isr14, 0x08, 0x8e);
	idt_set_gate(15, (uint32)isr15, 0x08, 0x8e);
	idt_set_gate(16, (uint32)isr16, 0x08, 0x8e);
	idt_set_gate(17, (uint32)isr17, 0x08, 0x8e);
	idt_set_gate(18, (uint32)isr18, 0x08, 0x8e);
	idt_set_gate(19, (uint32)isr19, 0x08, 0x8e);
	idt_set_gate(20, (uint32)isr20, 0x08, 0x8e);
	idt_set_gate(21, (uint32)isr21, 0x08, 0x8e);
	idt_set_gate(22, (uint32)isr22, 0x08, 0x8e);
	idt_set_gate(23, (uint32)isr23, 0x08, 0x8e);
	idt_set_gate(24, (uint32)isr24, 0x08, 0x8e);
	idt_set_gate(25, (uint32)isr25, 0x08, 0x8e);
	idt_set_gate(26, (uint32)isr26, 0x08, 0x8e);
	idt_set_gate(27, (uint32)isr27, 0x08, 0x8e);
	idt_set_gate(28, (uint32)isr28, 0x08, 0x8e);
	idt_set_gate(29, (uint32)isr29, 0x08, 0x8e);
	idt_set_gate(30, (uint32)isr30, 0x08, 0x8e);
	idt_set_gate(31, (uint32)isr31, 0x08, 0x8e);

	/* Reprogram the PICs, so that IRQs don't overlop with CPU exception interrupts */
	outb(0x20, 0x11);
	outb(0xA0, 0x11);
	outb(0x21, 0x20);
	outb(0xA1, 0x28);
	outb(0x21, 0x04);
	outb(0xA1, 0x02);
	outb(0x21, 0x01);
	outb(0xA1, 0x01);
	outb(0x21, 0x0);
	outb(0xA1, 0x0);
 
	/* Add IRQ handlers to our IDT */
	idt_set_gate(32, (uint32)irq0, 0x08, 0x8e);
    idt_set_gate(33, (uint32)irq1, 0x08, 0x8e);
    idt_set_gate(34, (uint32)irq2, 0x08, 0x8e);
    idt_set_gate(35, (uint32)irq3, 0x08, 0x8e);
    idt_set_gate(36, (uint32)irq4, 0x08, 0x8e);
    idt_set_gate(37, (uint32)irq5, 0x08, 0x8e);
    idt_set_gate(38, (uint32)irq6, 0x08, 0x8e);
    idt_set_gate(39, (uint32)irq7, 0x08, 0x8e);
    idt_set_gate(40, (uint32)irq8, 0x08, 0x8e);
    idt_set_gate(41, (uint32)irq9, 0x08, 0x8e);
    idt_set_gate(42, (uint32)irq10, 0x08, 0x8e);
    idt_set_gate(43, (uint32)irq11, 0x08, 0x8e);
    idt_set_gate(44, (uint32)irq12, 0x08, 0x8e);
    idt_set_gate(45, (uint32)irq13, 0x08, 0x8e);
    idt_set_gate(46, (uint32)irq14, 0x08, 0x8e);
    idt_set_gate(47, (uint32)irq15, 0x08, 0x8e);

	/* Load the new IDT */
	idt_load();
}

char *exception_name[] = {
	"Division by Zero (#DE)", // 0
	"Debug/Reserved (#DB)", // 1
	"NMI Interrupt", // 2
	"Breakpoint (#BP)", // 3
	"Overflow (#OF)", // 4
    "Out of Bounds (#BR)",
    "Invalid Opcode (#UD)",
    "No Coprocessor (#NM)",

    "Double Fault (#DF)", // 8
    "Coprocessor Segment Overrun",
    "Bad TSS (#TS)",
    "Segment Not Present (#NP)",
    "Stack Fault (#SS)",
    "General Protection Fault (#GP)",
    "Page Fault (#PF)",
    "Unknown Interrupt/Reserved",

    "x86 FPU Error/Math Fault (#MF)", // 16
    "Alignment Check (#AC)",
    "Machine Check (#MC)",
    "SIMD Floating-Point Exception (#XM)", // 19
    "Reserved", // 20
    "Reserved",
    "Reserved",
    "Reserved",

    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved" // 31
};

/* Called from the assembly code in kernel.s */
void isr_handler(registers_t regs) {
	printk("Received interrupt: %d (%s)\n", regs.int_no, exception_name[regs.int_no]);

	printk("EAX=%08x    EBX=%08x    ECX=%08x    EDX=%08x\n", regs.eax, regs.ebx, regs.ecx, regs.edx);
	printk("ESI=%08x    EDI=%08x    ESP=%08x    EBP=%08x\n", regs.esi, regs.edi, regs.esp, regs.ebp);
	printk("CS =%08x    EIP=%08x    EFLAGS=%08x USERESP=%08x\n", regs.cs, regs.eip, regs.eflags, regs.useresp);
	printk("INT=%02dd         ERR_CODE=0x%04x   DS=%08x\n", regs.int_no, regs.err_code, regs.ds);

	if (interrupt_handlers[regs.int_no] != 0) {
		isr_t handler = interrupt_handlers[regs.int_no];
		handler(regs);
	}
	else {
		panic("Interrupt not handled (no handler registered for interrupt number)");
	}
}

/* Called from the assembly code in kernel.s */
void irq_handler(registers_t regs) {
	/* If this interrupt came from the slave PIC, send an
	   EOI (End Of Interrupt) ta it */
	if (regs.int_no >= IRQ8) {
		outb(0xa0, 0x20);
	}

	/* Send an EOI to the master PIC in either case, since slave IRQs goes through it anyway */
	outb(0x20, 0x20);

	/* Call the interrupt handler, if there is one. */
	if (interrupt_handlers[regs.int_no] != 0) {
		isr_t handler = interrupt_handlers[regs.int_no];
		handler(regs);
	}
}
