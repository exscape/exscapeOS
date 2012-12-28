.global _start
.extern main
.extern exit
.extern _init_signal
_start:

# set up Newlib signals (intra-process only), until
# exscapeOS supports real signals
call _init_signal

# push argc and argv
	movl 4(%esp), %eax
	movl 8(%esp), %ebx
	pushl %ebx
	pushl %eax
# run the program
	call main
# clean the parameters off the stack
	addl $0x8, %esp

# call exit() with the main() return value as a parameter
	pushl %eax
	call exit

# this should not be reached,
# but in case it is, wait forever
	.wait:
	hlt
	jmp .wait
