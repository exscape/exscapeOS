.global _start
.extern main
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

# call _exit
	movl %eax, %ebx # set first argument to _exit syscall
	movl $0, %eax # _exit is syscall 0
	int $0x80

# this should not be reached,
# but in case it is, wait forever
	.wait:
	hlt
	jmp .wait
