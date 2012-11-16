#include <kernel/syscall.h>
#include <kernel/console.h> /* puts */
#include <kernel/interrupts.h>
#include <kernel/task.h>

static uint32 syscall_handler(uint32);

void exit_proc(void); // task.c

static void *syscalls[] = {
	&puts,
	&exit_proc,
	&sleep,
};
uint32 num_syscalls = 3;

void init_syscalls(void) {
	register_interrupt_handler(0x80, &syscall_handler);
}

uint32 syscall_handler(uint32 esp) {
	registers_t *regs = (registers_t *)esp;
	/* Make sure this is a valid syscall */
	if (regs->eax >= num_syscalls)
		return esp;

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

	return esp;
}

DEFN_SYSCALL1(puts, 0, const char *);
DEFN_SYSCALL0(exit_proc, 1);
DEFN_SYSCALL1(sleep, 2, uint32);
