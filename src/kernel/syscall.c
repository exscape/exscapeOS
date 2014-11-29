#include <kernel/syscall.h>
#include <kernel/console.h>
#include <kernel/interrupts.h>
#include <kernel/task.h>
#include <kernel/kernutil.h>
#include <kernel/vfs.h>
#include <kernel/backtrace.h>
#include <sys/time.h>

// strace-like mechanism that prints all syscalls and their parameters
#define SYSCALL_DEBUG 0

static uint32 syscall_handler(uint32);

void _exit(int); // task.c

struct syscall_entry {
	void *func;
	uint8 num_args; // number of arguments, 0 to 5
	uint8 return_size; // 32 or 64
};

// Syscalls that have extra checking due to userspace pointers, etc.
int sys_open(const char *path, int mode);
int sys_write(int fd, const void *buf, int length);
int sys_read(int fd, void *buf, int length);
int sys_stat(const char *path, struct stat *buf);
int sys_lstat(const char *path, struct stat *buf);
int sys_chdir(const char *in_path);
int sys_fstat(int fd, struct stat *buf);
int sys_getdents(int fd, void *dp, int count);
int sys_gettimeofday(struct timeval *restrict tp, void *restrict tzp);
int sys_nanosleep(const struct timespec *rqtp, struct timespec *rmtp __attribute__((unused)));
int sys_wait(int *status);
int sys_waitpid(int pid, int *status, int options);
int sys_execve(const char *path, char **argv, char **envp);
char *sys_getcwd(char *buf, size_t size);
int sys_pipe(int fildes[2]);
ssize_t sys_readlink(const char *pathname, char *buf, size_t bufsiz);

struct syscall_entry syscalls[] = {
/*  { &function, num_args, return_size }, */
	{ &_exit, 1, 32 },   /* 0 */
	{ &puts, 1, 32 },
	{ &sleep, 1, 32 },
	{ &getchar, 0, 32 },
	{ &putchar, 1, 32 },
	{ &sys_open, 2, 32 },   /* 5 */
	{ &sys_read, 3, 32 },
	{ &close, 1, 32 },
	{ NULL, 1, 32 }, // old malloc
	{ NULL, 1, 32 }, // old free
	{ &sys_stat, 2, 32 },   /* 10 */
	{ &sys_chdir, 1, 32 },
	{ &sys_write, 3, 32 },
	{ &lseek, 4, 64 }, // 3 arguments, but one is a 64-bit arg split in two
	{ &sys_fstat, 2, 32 },
	{ &getpid, 0, 32 }, /* 15 */
	{ &sbrk, 1, 32 },
	{ &__getreent, 0, 32 },
	{ &sys_getdents, 3, 32 },
	{ &sys_gettimeofday, 2, 32 },
	{ &fork, 0, 32}, /* 20 */
	{ &sys_nanosleep, 2, 32},
	{ &sys_wait, 1, 32 },
	{ &getppid, 0, 32 },
	{ &sys_waitpid, 3, 32 },
	{ &sys_execve, 3, 32 }, /* 25 */
	{ &sys_getcwd, 2, 32 },
	{ &dup, 1, 32 },
	{ &dup2, 2, 32 },
	{ &sys_pipe, 1, 32 },
	{ &sys_lstat, 2, 32 }, /* 30 */
	{ &sys_readlink, 3, 32 }
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
	if (regs->eax >= num_syscalls) {
		panic("syscall_handler: syscall index %d out of range (no such syscall)", regs->eax);
		return esp; // silence warnings
	}

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

#if SYSCALL_DEBUG > 0
	struct symbol *sym = addr_to_func((uint32)func);
	if (sym && sym->name && regs->eax != 13 /* lseek is a special case */) {
		printk("syscall: %s(", sym->name);

		switch (syscalls[regs->eax].num_args) {
			case 0:
				break;
			case 1:
				printk("0x%x", regs->ebx);
				break;
			case 2:
				printk("0x%x, 0x%x", regs->ebx, regs->ecx);
				break;
			case 3:
				printk("0x%x, 0x%x, 0x%x", regs->ebx, regs->ecx, regs->edx);
				break;
			case 4:
				printk("0x%x, 0x%x, 0x%x, 0x%x", regs->ebx, regs->ecx, regs->edx, regs->esi);
				break;
			case 5:
				printk("0x%x, 0x%x, 0x%x, 0x%x, 0x%x", regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi);
				break;
			default:
				panic("\?\?\?)\nsyscall %s has >5 args in syscall table, which is not supported\n", sym->name);
		}

		printk(")\n");
	} else if (sym && sym->name && regs->eax == 13) { // lseek
		if (regs->edx != 0)
			printk("syscall: lseek(0x%x, 0x%x%x, 0x%x)\n", regs->ebx, regs->edx, regs->ecx, regs->esi);
		else
			printk("syscall: lseek(0x%x, 0x%x, 0x%x)\n", regs->ebx, regs->ecx, regs->esi);
	}
	else
		printk("syscall: <unknown function>(0x%x, 0x%x, 0x%x, 0x%x, 0x%x) (unknown # of args)\n",
			   regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi);
#endif

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
