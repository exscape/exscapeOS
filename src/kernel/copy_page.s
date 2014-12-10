section .text
align 4
global copy_page_physical:function

copy_page_physical:
	pushf       ; push EFLAGS
	cli
	push esi    ; we can only trample eax, ecx and edx
	push edi

	; NOTE: if the number of pushes above changes, these offsets change as well!
	mov esi, [esp+16] ; source address
	mov edi, [esp+20] ; dest address

	; Disable paging
	mov edx, cr0
	and edx, 0x7fffffff
	mov cr0, edx

	; Copy the data
	mov ecx, 1024 ; 1024*4 bytes to copy
	rep movsd

	; Re-enable paging
	mov edx, cr0
	or edx, 0x80000000
	mov cr0, edx

	pop edi
	pop esi
	popf  ; return EFLAGS, which will also re-enable interrupts
	ret
