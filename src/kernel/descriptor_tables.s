global gdt_flush
global idt_load
global tss_flush
extern gp              ; the GDT pointer, defined in kernel/gdt.c
extern idtp            ; the IDT pointer, defined in kernel/idt.c

section .text
align 4

gdt_flush:
	lgdt [gp]    ; load the GDT
	mov ax, 0x10  ; 0x10 is the offset to the data segment in the GDT, i.e gdt[2] in C
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax
	jmp 0x08:flush2 ; 0x08 is the offset to the code segment (gdt[1]), so this is a far jump there

flush2:
	ret ; jump back to the C kernel

; ----------

idt_load:
	lidt [idtp]
	ret         ; Yup, that's it!

tss_flush:
	mov ax, 0x2b ; load the index of our TSS structure - 0x28 | 3 (for user mode)
	ltr ax
	ret
