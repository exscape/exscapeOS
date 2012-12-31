.global _start
.extern main
.extern exit
.extern environ
.extern _init_signal

_start:

# set up Newlib signals (intra-process only), until
# exscapeOS supports real signals
call _init_signal

# push argc and argv, and set up environ
	movl 4(%esp), %eax # argc
	movl 8(%esp), %ebx # argv
	movl 12(%esp), %ecx # envp
	pushl %ecx # envp
	pushl %ebx # argv
	pushl %eax # argc
	mov %ecx, environ
# run the program
	call main
# clean the parameters off the stack
	addl $12, %esp

# call exit() with the main() return value as a parameter
	pushl %eax
	call exit

# this should not be reached,
# but in case it is, wait forever
	.wait:
	hlt
	jmp .wait
