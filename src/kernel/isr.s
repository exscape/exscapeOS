extern isr_handler     ; defined in kernel/interrupts.c
extern irq_handler     ; defined in kernel/interrupts.c
extern in_isr          ; kernel/interrupts.c (bool)

section .text
align 4

; -----------
; ISR routines

%macro ISR_NOERRCODE 1
	global isr%1:function
	isr%1:
		cli
		push 0
		push %1
		jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
	global isr%1:function
	isr%1:
		cli
		push %1
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

; the syscall handler
ISR_NOERRCODE 128

; Our ISR stub. It saves the processor state, sets up for kernel mode segments,
; calls the C-level fault handler, and restores the stack frame.
global isr_common_stub:function
isr_common_stub:
	mov byte [in_isr], 1
	push eax
    push ecx
    push edx
    push ebx
    push ebp
    push esi
    push edi

    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

	push esp
	call isr_handler
	mov esp, eax

	pop gs
    pop fs
    pop es
    pop ds

    pop edi
    pop esi
    pop ebp
    pop ebx
    pop edx
    pop ecx
    pop eax

    add esp, 8
	mov byte [in_isr], 0
	sti
    iret


; ---------
; IRQ handling

; Create a stub handler, since each IRQ needs its own function
%macro IRQ 2 ; 2 arguments, IRQ number and the ISR number we map it to
	global irq%1:function
	irq%1:
		cli
		push byte 0 ; IRQ handlers (like ISR handlers) use the registers_t struct, so we need this dummy "error code"
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
IRQ 126,    126 ; the task switch vector

; This is called/jumped to by the handlers created by the macros above.
global irq_common_stub:function
irq_common_stub:
	; interrupts are already off
	mov byte [in_isr], 1
	push eax
    push ecx
    push edx
    push ebx
    push ebp
    push esi
    push edi

    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

	push esp
	call irq_handler
	mov esp, eax

	pop gs
    pop fs
    pop es
    pop ds

    pop edi
    pop esi
    pop ebp
    pop ebx
    pop edx
    pop ecx
    pop eax

    add esp, 8
	mov byte [in_isr], 0
	sti
    iret
