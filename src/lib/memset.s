section .text
align 4
global memset

; void *memset(void *addr, int c, size_t n);
; returns the input addr

memset:
	; set up the stack frame
	push ebp
	mov ebp, esp

	; save registers (we can only destroy EAX, ECX and EDX)
	push ebx
	push edi

;	slosl: Fill (E)CX dwords at ES:[(E)DI] with EAX
;	stosb: Fill (E)CX bytes at ES:[(E)DI] with AL

	mov edi, [ebp + 8]  ; param #1 (addr)
	mov ebx, [ebp + 12] ; param #2 (character)
	mov ecx, [ebp + 16] ; param #3 (length)

	; push the first argument; it's our return value
	push edi

	; set up the value to write; since we should only use the lower 8 bits of it, we need to repeat it.
	; i.e. the input may be 0x000000ab (meaning the user specified 0xab; the rest is padding for the int type used),
	; meaning we want to write the dword 0xabababab.
	; bl contains the bits to use and repeat.
	and ebx, 0xff  ; clear any other bits
	xor eax, eax ; clear the target register
	or eax, ebx ; add the lowest two bits, 0x000000YY
	shl ebx, 8
	or eax, ebx ; add 0x0000YY00 bits
	shl ebx, 8
	or eax, ebx ; add 0x00YY0000 bits
	shl ebx, 8
	or eax, ebx ; add 0xYY000000 bits

	; eax should now contain the pattern we want (for stosd), e.g. 0xabababab for the input 0xab
	; likewise, al should contain the pattern we want (for stosb)

	push eax ; store the value (temporarily, while we calculate the number of dwords and bytes)

	; Set ecx to the number of dwords to set
	mov eax, ecx ; ecx = number of bytes to set
	xor edx, edx ; clear edx before division
	mov ebx, 4   ; div doesn't accept immediate operands
	div ebx
	mov ecx, eax ; len/4 dwords to copy (since each is 4 bytes!)
	mov ebx, edx ; len%4 bytes left; store this in ebx for the moment

	pop eax ; fetch the value to set into eax

	rep stosd    ; set the dwords
	mov ecx, ebx ; ebx = number of bytes remaining (i.e. len % 4)
	rep stosb    ; set the bytes, if any

	; pop the return value
	pop eax

	; restore the registers we pushed
	pop edi
	pop ebx

	leave ; clean up the stack frame
	ret
