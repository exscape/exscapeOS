section .text
align 4
global internet_checksum

internet_checksum:
   ; set up the stack frame
   push ebp
   mov ebp, esp

	push ebx ; eax, ecx and edx can be trampled

	mov ebx, [ebp + 8]  ; param 1 (data pointer)
	mov ecx, [ebp + 12] ; param 2 (length)

	xor ax, ax ; we store the sum here

	.loop:
		sub ecx, 2 ; if len = 20, data[20] is an invalid access! start off at length-2
		add ax, word [ebx + ecx]
		jc .addone
	.back:
		cmp ecx, 1
		je .lastbyte
		cmp ecx, 0
		jnz .loop
		jmp .end

	.addone:
		inc ax
		jmp .back

	.lastbyte:
		movzx dx, byte [ebx]
		add ax, dx
		jnc .end
		inc ax

	.end:
	pop ebx ; restore ebx
	not ax
	movzx eax, ax

	leave
	ret
