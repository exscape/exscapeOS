section .text
align 4
global copy_page_physical

copy_page_physical:
	push ebx    ; we can only trample eax, ecx and edx
	pushf       ; push EFLAGS
	cli

	mov ebx, [esp+12] ; source address
	mov ebx, [esp+16] ; dest address

	; Disable paging
	mov edx, cr0
	and edx, 0x7fffffff
	mov cr0, edx

	mov edx, 1024 ; 1024*4 bytes to copy

	.loop:
	; TODO: rep movsd? performance should be important 'round these parts
		mov eax, [ebx]
		mov [ecx], eax
		add ebx, 4
		add ecx, 4
		dec edx
		jnz .loop

	; Re-enable paging
	mov edx, cr0
	or edx, 0x80000000
	mov cr0, edx

	popf  ; return EFLAGS, which will also re-enable interrupts
	pop ebx ; restore ebx
	ret
