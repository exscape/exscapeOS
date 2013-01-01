#include <kernel/syscall.h>
#include <kernel/console.h>
#include <kernel/interrupts.h>
#include <kernel/task.h>
#include <kernel/kernutil.h>
#include <kernel/vfs.h>
#include <sys/time.h>

static uint32 syscall_handler(uint32);

void _exit(int); // task.c

struct syscall_entry {
	void *func;
	uint8 return_size; // 32 or 64
};

// Syscalls that have extra checking due to userspace pointers, etc.
int sys_open(const char *path, int mode);
int sys_write(int fd, const void *buf, int length);
int sys_read(int fd, void *buf, int length);
int sys_stat(const char *path, struct stat *buf);
int sys_chdir(const char *in_path);
int sys_fstat(int fd, struct stat *buf);
int sys_getdents(int fd, void *dp, int count);
int sys_gettimeofday(struct timeval *restrict tp, void *restrict tzp);
int sys_nanosleep(const struct timespec *rqtp, struct timespec *rmtp __attribute__((unused)));
int sys_wait(int *status);
int sys_waitpid(int pid, int *status, int options);
int sys_execve(const char *path, char **argv, char **envp);
char *sys_getcwd(char *buf, size_t size);

struct syscall_entry syscalls[] = {
/*  { &function, return_size }, */
	{ &_exit, 32 },   /* 0 */
	{ &puts, 32 },
	{ &sleep, 32 },
	{ &getchar, 32 },
	{ &putchar, 32 },
	{ &sys_open, 32 },   /* 5 */
	{ &sys_read, 32 },
	{ &close, 32 },
	{ NULL, 32 }, // old malloc
	{ NULL, 32 }, // old free
	{ &sys_stat, 32 },   /* 10 */
	{ &sys_chdir, 32 },
	{ &sys_write, 32 },
	{ &lseek, 64 },
	{ &sys_fstat, 32 },
	{ &getpid, 32 }, /* 15 */
	{ &sbrk, 32 },
	{ &__getreent, 32 },
	{ &sys_getdents, 32 },
	{ &sys_gettimeofday, 32 },
	{ &fork, 32},
	{ &sys_nanosleep, 32},
	{ &sys_wait, 32 },
	{ &getppid, 32 },
	{ &sys_waitpid, 32 },
	{ &sys_execve, 32},
	{ &sys_getcwd, 32},
	{ &dup, 32},
	{ &dup2, 32},
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

	// Old, removed syscalls could cause this to happen, which would point to
	// a Newlib syscalls.c bug that needs fixing.0
	assert(func != NULL);

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
