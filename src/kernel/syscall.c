#include <kernel/syscall.h>
#include <kernel/console.h>
#include <kernel/interrupts.h>

static void syscall_handler(uint32);

static void *syscalls[] = {
	&print,
};
uint32 num_syscalls = 1;

void init_syscalls(void) {
	register_interrupt_handler(0x80, &syscall_handler);
}

void syscall_handler(uint32 esp) {
	registers_t *regs = (registers_t *)esp;
	/* Make sure this is a valid syscall */
	if (regs->eax >= num_syscalls)
		return;

	/* Get the function */
	void *func = syscalls[regs->eax];

	/* Since we don't know how many arguments the function needs, pass along
	 * them all, and let it use however many it needs to. */
	int ret;

	asm volatile("push %1;"
			     "push %2;"
				 "push %3;"
				 "push %4;"
				 "push %5;"
				 "call *%6;"
				 "pop %%ebx;"
				 "pop %%ebx;"
				 "pop %%ebx;"
				 "pop %%ebx;"
				 "pop %%ebx;"
				 : "=a" (ret) : "r" (regs->edi), "r" (regs->esi), "r" (regs->edx), "r" (regs->ecx), "r" (regs->ebx), "r" (func));

	regs->eax = ret;
}

DEFN_SYSCALL1(print, 0, const char *);
