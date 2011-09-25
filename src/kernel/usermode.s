global switch_to_user_mode

section .text
;align 4

switch_to_user_mode:

; Set up the segment registers and the stack for user mode

; iret expects the following on the stack:
 ; 1) an EIP value to jump to
 ; 2) a CS selector
 ; 3) an EFLAGS register to load
 ; 4) a stack pointer to load
 ; 5) a stack segment selector

	cli
	mov ax, 0x23 ; user mode data segment (0x20) OR 3 (for ring 3)
	mov ds, ax   ; set all the data segments to the above
	mov es, ax
	mov fs, ax
	mov gs, ax

	mov eax, esp
	push 0x23     ; SS (0x20 | 3)
	push eax      ; ESP
	pushfd        ; EFLAGS
;;	or [esp], 0x200 ; set IF in the flags ; TODO: enable this (disabled for testing) 
	push 0x1b     ; CS (0x18 | 3)
	push start_here ; EIP
	iret
	start_here:
	ret
