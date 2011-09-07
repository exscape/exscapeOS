global gdt_flush
global idt_load
extern gp              ; the GDT pointer, defined in kernel/gdt.c
extern idtp            ; the IDT pointer, defined in kernel/idt.c
extern isr_handler     ; defined in kernel/idt.c
extern irq_handler     ; defined in kernel/idt.c

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

; -----------
; ISR routines

%macro ISR_NOERRCODE 1
	[GLOBAL isr%1]
	isr%1:
		cli
		push byte 0
		push byte %1
		jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
	[GLOBAL isr%1]
	isr%1:
		cli
		push byte %1
		jmp isr_common_stub
%endmacro

ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE 8
ISR_NOERRCODE 9
ISR_ERRCODE 10
ISR_ERRCODE 11
ISR_ERRCODE 12
ISR_ERRCODE 13
ISR_ERRCODE 14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE 17   ; Is this correct? The Intel manual states that a 0 error code is pushed.
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31

; Our ISR stub. It saves the processor state, sets up for kernel mode segments,
; calls the C-level fault handler, and restores the stack frame.
isr_common_stub:
	pusha     ; edi, esi, ebp, esp, ebx, edx, ecx, eax
	mov ax, ds
	push eax

	mov ax, 0x10  ; load the kernel data segment descriptor
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax

	call isr_handler

	pop eax       ; reload the original data segment descriptor
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax

	popa
	add esp, 8    ; clean up the pushed error code and ISR number
	sti           ; FIXME: why do we enable interrupts here, when we never disable them?
	iret

; ---------
; IRQ handling

; Create a stub handler, since each IRQ needs its own function
%macro IRQ 2 ; 2 arguments, IRQ number and the ISR number we map it to
	[GLOBAL irq%1]
	irq%1:
		cli
		push byte 0  ; FIXME: is this needed?
		push byte %2
		jmp irq_common_stub
%endmacro

; Create the IRQ handlers and map them to better ISR numbers
IRQ   0,    32
IRQ   1,    33
IRQ   2,    34
IRQ   3,    35
IRQ   4,    36
IRQ   5,    37
IRQ   6,    38
IRQ   7,    39
IRQ   8,    40
IRQ   9,    41
IRQ  10,    42
IRQ  11,    43
IRQ  12,    44
IRQ  13,    45
IRQ  14,    46
IRQ  15,    47

; This is called/jumped to by the handlers created by the macros above.
irq_common_stub:
	pusha

	mov ax, ds
	push eax ; same the data segment descriptor

	mov ax, 0x10 ; load the kernel data segment descriptor
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax

	call irq_handler ; implemented in C

	pop ebx ; reload the original data segment descriptor
	mov ds, bx
	mov es, bx
	mov fs, bx
	mov gs, bx

	popa ; pop the rest
	add esp, 8 ; clean up the pushed error code/ISP number // FIXME, in case the dummy is removed later on (in the stubs, above)
	sti  ; FIXME: why? do interrupts automatically disable the interrupt flag?
	iret