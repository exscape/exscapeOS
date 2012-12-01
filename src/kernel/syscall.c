#include <kernel/syscall.h>
#include <kernel/console.h> /* puts, getchar, putchar */
#include <kernel/interrupts.h>
#include <kernel/task.h> /* sleep, exit_proc */
#include <kernel/kernutil.h>
#include <kernel/fileio.h> /* open, read */

static uint32 syscall_handler(uint32);

void exit_proc(void); // task.c

static void *syscalls[] = {
	&exit_proc,
	&puts,
	&sleep,
	&getchar,
	&putchar,
	&open,
	&read,
	&malloc,
	&free
};

uint32 num_syscalls = 0;

void init_syscalls(void) {
	register_interrupt_handler(0x80, &syscall_handler);
	num_syscalls = sizeof(syscalls) / sizeof(void *);
}

uint32 syscall_handler(uint32 esp) {
	registers_t *regs = (registers_t *)esp;
	/* Make sure this is a valid syscall */
	if (regs->eax >= num_syscalls)
		return esp;

	/* Get the function */
	void *func = syscalls[regs->eax];

	enable_interrupts();

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

// This include defines the syscalls (not just declares). In order to have
// e.g. both syscall_puts() (for in-kernel user space code) and puts() for user space,
// without having to maintain TWO lists of syscalls, this hack is done instead.
#include "../../userspace/include/exscapeos.h"
