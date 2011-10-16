section .text
align 4
global memsetw

; void *memsetw(void *addr, uint16 word, size_t n);
; returns the input addr

memsetw:
	; set up the stack frame
	push ebp
	mov ebp, esp

	; save registers (we can only destroy EAX, ECX and EDX)
	push ebx
	push edi

	mov edi, [ebp + 8]  ; param #1 (addr)
	mov ebx, [ebp + 12] ; param #2 (word to fill with)
	mov ecx, [ebp + 16] ; param #3 (length (number of words))

	; push the first argument; it's our return value
	push edi

	; set up the value to write
	and ebx, 0xffff  ; clear any other bits (this should be a noop?)
	xor eax, eax ; clear the target register
	or eax, ebx ; add the low 16 bits (0x0000YYYY)
	shl ebx, 16
	or eax, ebx ; add the high 16 bits (0xYYYY0000)

	; eax should now contain the pattern we want (for stosd)
	; likewise, ax should contain the pattern we want (for stosw)

	push eax ; store the value (temporarily, while we calculate the number of dwords and words)

	; Set ecx to the number of dwords to set
	mov eax, ecx ; ecx = number of words to set
	xor edx, edx ; clear edx before division
	mov ebx, 2   ; div doesn't accept immediate operands
	div ebx
	mov ecx, eax ; len/2 dwords to copy
	mov ebx, edx ; len%2 words left; store this in ebx for the moment

	pop eax ; fetch the value to set into eax

	rep stosd    ; set the dwords
	mov ecx, ebx ; ebx = number of words remaining (i.e. len % 2 - 0 or 1)
	rep stosw    ; set the word, if any

	; pop the return value
	pop eax

	; restore the registers we pushed
	pop edi
	pop ebx

	leave ; clean up the stack frame
	ret
