section .text
align 4
global memcpy

memcpy:
	; set up the stack frame
	push ebp
	mov ebp, esp

	; save registers (we can only destroy EAX, ECX and EDX)
	push ebx
	push edi
	push esi

	mov edi, [ebp + 8]  ; param #1 (dst)
	mov esi, [ebp + 12] ; param #2 (src)
	mov eax, [ebp + 16] ; param #3 (len)

	push edi ; save the first argument, since we need to return it

	; Set ecx to the number of dwords to copy
	xor edx, edx ; clear edx before division
	mov ebx, 4   ; div doesn't accept immediate operands
	div ebx
	mov ecx, eax ; len/4 dwords to copy (since each is 4 bytes!)
	mov ebx, edx ; len%4 bytes left; store this in ebx for the moment

	rep movsd    ; copy the dwords
	mov ecx, ebx ; ebx = number of bytes remaining
	rep movsb    ; copy the bytes, if any

	; pop the return value
	pop eax

	; restore the registers we pushed
	pop esi
	pop edi
	pop ebx

	leave ; clean up the stack frame
	ret
