extern main

global _start
_start:
	mov eax, [esp + 4]
	mov ebx, [esp + 8]
	push ebx
	push eax
	call main
	add esp, 8

	mov ebx, eax ; set first argument to exit syscall
	mov eax, 0 ; exit is syscall 0
	int 0x80
