section .text
align 4
global zero_page_physical:function

zero_page_physical:
	push esi    ; we can only trample eax, ecx and edx
	push edi

	; NOTE: if the number of pushes above changes, this offset changes as well!
	mov edi, [esp+12] ; dest address

	; Disable paging
	mov edx, cr0
	and edx, 0x7fffffff
	mov cr0, edx

	; Zero the framae
	xor eax, eax
	mov ecx, 1024
	rep stosd

	; Re-enable paging
	mov edx, cr0
	or edx, 0x80000000
	mov cr0, edx

	pop edi
	pop esi
	ret
