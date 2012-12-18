#include <kernel/syscall.h>
#include <kernel/console.h>
#include <kernel/interrupts.h>
#include <kernel/task.h>
#include <kernel/kernutil.h>
#include <kernel/vfs.h>
#include <sys/time.h>

static uint32 syscall_handler(uint32);

void _exit(void); // task.c

struct syscall_entry {
	void *func;
	uint8 return_size; // 32 or 64
};

struct syscall_entry syscalls[] = {
/*  { &function, return_size }, */
	{ &_exit, 32 },   /* 0 */
	{ &puts, 32 },
	{ &sleep, 32 },
	{ &getchar, 32 },
	{ &putchar, 32 },
	{ &open, 32 },   /* 5 */
	{ &read, 32 },
	{ &close, 32 },
	{ &malloc, 32 },
	{ &free, 32 },
	{ &stat, 32 },   /* 10 */
	{ &chdir, 32 },
	{ &write, 32 },
	{ &lseek, 64 },
	{ &fstat, 32 },
	{ &getpid, 32 }, /* 15 */
	{ &sbrk, 32 },
	{ &__getreent, 32 },
	{ &getdents, 32 },
	{ &gettimeofday, 32 }
};

uint32 num_syscalls = 0;

void init_syscalls(void) {
	register_interrupt_handler(0x80, &syscall_handler);
	num_syscalls = sizeof(syscalls) / sizeof(struct syscall_entry);
}

extern volatile bool in_isr;

uint32 syscall_handler(uint32 esp) {
	registers_t *regs = (registers_t *)esp;
	/* Make sure this is a valid syscall */
	if (regs->eax >= num_syscalls)
		return esp;

	/* Get the function */
	void *func = syscalls[regs->eax].func;

	enable_interrupts();
	in_isr = false;

	/* Since we don't know how many arguments the function needs, pass along
	 * them all, and let it use however many it needs to. */
	int ret;
	uint32 ret64[2] = {0};

	if (syscalls[regs->eax].return_size == 32) {
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
				 : "=a" (ret)
				 : "r" (regs->edi), "r" (regs->esi), "r" (regs->edx), "r" (regs->ecx), "r" (regs->ebx), "r" (func)
				 : "cc", "memory");

	regs->eax = ret;
	}
	else {
	assert(syscalls[regs->eax].return_size == 64);
	asm volatile("push %[edi];"
			     "push %[esi];"
				 "push %[edx];"
				 "push %[ecx];"
				 "push %[ebx];"
				 "call *%[func];"
				 "pop %%ebx;"
				 "pop %%ebx;"
				 "pop %%ebx;"
				 "pop %%ebx;"
				 "pop %%ebx;"
				 : "=a" (ret64[1]), "=d" (ret64[0])
				 : [edi]"r" (regs->edi), [esi]"r" (regs->esi), [edx]"r" (regs->edx), [ecx]"r" (regs->ecx), [ebx]"r" (regs->ebx), [func]"r" (func)
				 : "cc", "memory");

	regs->eax = ret64[1];
	regs->edx = ret64[0];
	}

	return esp;
}
