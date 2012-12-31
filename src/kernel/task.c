#include <sys/types.h>
#include <string.h>
#include <kernel/kernutil.h>
#include <kernel/heap.h>
#include <kernel/vmm.h>
#include <kernel/console.h>
#include <kernel/task.h>
#include <kernel/gdt.h>
#include <kernel/timer.h>
#include <kernel/list.h>
#include <kernel/syscall.h>
#include <kernel/vfs.h>
#include <kernel/elf.h>
#include <path.h>
#include <kernel/stdio.h>
#include <reent.h>
#include <sys/time.h> /* struct timespec */
#include <sys/wait.h>
#include <sys/errno.h>

/*
 * Here's a overview of how the multitasking works in exscapeOS.
 * The PIT raises an interrupt (IRQ0) every 10 ms. The IRQ handler
 * automatically saves all registers, EFLAGS and EIP on the stack
 * of the current (to-be-switched-out) task.
 * After deciding which task to switch to, the task switching code
 * updates the TSS, switches to the correct page directory, etc.,
 * and returns the ESP of the new task. That points the CPU to 
 * the stack of the NEW task, where the ISR automatically pops off
 * the values of all the registers (except ESP, of course), 
 * plus EFLAGS, EIP and all that. After that's done, the IRET
 * instruction jumps to the EIP it just popped off, and the 
 * task has been switched!
 */

volatile bool task_switching = false;

task_t *next_task = NULL;
task_t *idle_task = NULL;

/* Size of the kernel stack for each task (except the main kernel task; that stack is set up in loader.s) */
#define KERNEL_STACK_SIZE 8192

uint32 next_pid = 1; /* kernel_task has PID 1 */

task_t kernel_task = {
	.id = 0,
	.esp = 0,
	.ss = 0x10,
	.stack = 0, /* set later */
	.mm = 0, /* set later */
	.state = TASK_RUNNING,
	.wakeup_time = 0,
	.console = &kernel_console,
};

/* Our globals */

/* Points to the currently executing task. In interrupt handlers, this points to the interrupted task. */
volatile task_t *current_task = &kernel_task;

/* The currently executing task, EXCEPT in ISRs/IRQ handlers (and all functions they call),
 * where this is set to kernel_task. Why? Because all kernel output should go to
 * the kernel console, and this is an easy way to do that. */
volatile task_t *console_task = &kernel_task;

extern volatile list_t ready_queue; /* "forward declare" the variable, since they link to each other */
volatile node_t kernel_task_node = {
	.data = (void *)&kernel_task,
	.prev = NULL,
	.next = NULL,
	.list = (list_t *)&ready_queue,
};

volatile list_t ready_queue = {
	.head = (node_t *)&kernel_task_node,
	.tail = (node_t *)&kernel_task_node,
	.count = 1,
	.mutex =  NULL, // Set up ASAP in kmain
};

/* true if the task exists and is running/sleeping; false if it has exited (or never even existed) */
bool does_task_exist(task_t *task) {
	return (list_find_first((list_t *)&ready_queue, (void *)task) != NULL);
}

extern list_t *pagedirs;

struct _reent *__getreent(void) {
	assert(current_task->privilege == 3);
	assert(current_task->reent != NULL);
	return current_task->reent;
}

extern task_t *reaper_task;

static int do_wait_one(task_t *parent, task_t *child, int *status) {
	assert(parent != NULL);
	assert(child != NULL);

	if (status != NULL)
		*status = child->exit_code;

	int child_pid = child->id;
	int a = parent->children->count;
	list_remove_first(parent->children, child);
	memset(child, 0, sizeof(task_t));
	kfree(child);
	assert(parent->children->count == (uint32)(a - 1));

	// Delete this task from the queue
	list_remove_first((list_t *)&ready_queue, child);

	return child_pid;
}

void destroy_task(task_t *task) {
	assert(task != &kernel_task);
	assert(task != current_task);
	assert(task->state == TASK_EXITING);

	task->state = TASK_DEAD;

	// Freed earlier on
	assert(task->console == NULL);

	if (task->children && task->children->count > 0) {
		// Take care of orphaned tasks

		assert(reaper_task != NULL);
		assert(reaper_task->children != NULL);

		INTERRUPT_LOCK;
		list_foreach(task->children, it) {
			task_t *child = (task_t *)it->data;
			// There ase two possibilities here, for each child that we encounter:
			// 1) It might be dead: in that case, we free the memory associated with it, and be done with it.
			// 2) It might be alive: in that case, we move it to a new parent process.
			if (child->state == TASK_DEAD) {
				list_remove_first((list_t *)&ready_queue, task);
				memset(child, 0, sizeof(task_t));
				kfree(child);
			}
			else {
				// Task is alive; move it.
				// And by move, I really mean "add it to another task": we don't want to
				// modify this list while iterating over it. Since we need to act on ALL
				// members either way, there's no point in modifying it. We'll destroy it
				// when this loop has exited.
				list_append(reaper_task->children, child);
				child->parent = reaper_task;
			}
		}
		INTERRUPT_UNLOCK;

	}

	if (task->children != NULL) {
		list_destroy(task->children);
		task->children = NULL;
	}

	// Free stuff in the file descriptor table (the table itself is in struct task)
	for (int i=0; i < MAX_OPEN_FILES; i++) {
		if (task->fdtable[i]) {
			assert(do_close(i, task) == 0); // only fails if there's a bug somewhere, since we only call it on non-NULL fds
			assert(task->fdtable[i] == NULL);
		}
	}

	// Free the table itself
	kfree(task->fdtable);
	task->fdtable = NULL;

	if (task->privilege == 3) {
		list_remove_first(pagedirs, task->mm->page_directory);
		destroy_user_page_dir(task->mm->page_directory);

		// Free all of this task's frames (user space stack, stuff loaded from ELF files, etc.)
		vmm_destroy_task_mm(task->mm);
		task->mm = NULL;
	}

	if (task->pwd)
		kfree(task->pwd);

	/* Free the kernel stack, after re-mapping the guard pages again */
	vmm_clear_guard((uint32)task->stack - KERNEL_STACK_SIZE - 4096, kernel_directory);
	vmm_clear_guard((uint32)task->stack, kernel_directory);
	kfree((void *)( (uint32)task->stack - KERNEL_STACK_SIZE - 2*4096 ));

	assert(task->children == NULL);

	if (task->parent != NULL) {
		assert(task->parent->state != TASK_DEAD);
		// The parent might be wait()ing on this task, either right now or later on.
		// We can't free this task just yet. Set the state and notify the scheduler,
		// in case the parent is wait()ing.
		if (task->parent->state == TASK_WAITING) {
			set_next_task(task->parent);
		}
		YIELD;
	}
	else {
		/* Delete this task from the queue */
		list_remove_first((list_t *)&ready_queue, task);
		memset(task, 0, sizeof(task_t));
		kfree(task);
	}
}

task_t *reaper_task = NULL;

void reaper_func(void *data, uint32 length) {
	while(true) {
		INTERRUPT_LOCK;
		list_foreach_dot(ready_queue, it) {
			task_t *p = (task_t *)it->data;
			if (p->state == TASK_EXITING) {
				destroy_task(p);
			}
			else if (p->state == TASK_DEAD) {
				uint32 o = current_task->children->count;
				do_wait_one((task_t *)current_task, p, NULL);
				assert(current_task->children->count == o - 1);
				break; // TODO: the list has been modified, so we need to restart. Fix this, such that it's safe to modify the list!
			}
		}
		INTERRUPT_UNLOCK;
		sleep(20); // TODO: make sure that higher values won't cause lag!
	}
}

bool kill_pid(int pid) {
	/* Kills the task with a certain PID */
	assert(pid != 1); /* kernel_task has PID 1 */

	INTERRUPT_LOCK;

	list_foreach_dot(ready_queue, it) {
		task_t *t = (task_t *)it->data;
		if (t->id == pid) {
			kill(t);
			INTERRUPT_UNLOCK;
			return true;
		}
	}

	INTERRUPT_UNLOCK;
	return false;
}

void kill(task_t *task) {
	INTERRUPT_LOCK;
	task->state = TASK_EXITING;
	current_task->exit_code = (1 << 8); // TODO: store signal (SIGKILL?) number + status; this is a normal exit with status 1!

	if (task->console != NULL) {
		/* Remove this task from the console chain */
		list_remove_first(task->console->tasks, task);
		task->console = NULL;
	}
	INTERRUPT_UNLOCK;
}

void _exit(int status) {
	kill((task_t *)current_task);
	current_task->exit_code = ((status & 0xff) << 8);
	YIELD;
	panic("this should never be reached (in _exit after switching tasks)");
}

void user_exit(void) {
	asm volatile("int $0x80" : : "a"(0 /* _exit syscall number */), "b"(0));
}

void idle_task_func(void *data, uint32 length) {
	while (true) {
		asm volatile("hlt");
	}
}

void init_tasking(uint32 kerntask_esp0) {
	disable_interrupts();

	kernel_task.mm = kmalloc(sizeof(struct task_mm));
	memset(kernel_task.mm, 0, sizeof(struct task_mm));
	kernel_task.mm->page_directory = kernel_directory;
	kernel_task.stack = (void *)kerntask_esp0;
	strlcpy(kernel_task.name, "[kernel_task]", TASK_NAME_LEN);

	reaper_task = create_task(reaper_func, "reaper", false, NULL, 0);
	assert(reaper_task->id == 1);

	idle_task = create_task(&idle_task_func, "idle_task", NULL, NULL, 0);
	idle_task->state = TASK_IDLE; /* TODO(?): should really be ->priority, but there is no such thing yet */

	task_switching = true;
	enable_interrupts();
}

char **parse_command_line(const char *cmdline, uint32 *argc, task_t *task) {
	// Count the worst-case number of arguments. There may be fewer due to quoting
	uint32 max_args = 1; // the command name is always there
	size_t len = strlen(cmdline);
	for (size_t i=0; i < len; i++) {
		if (cmdline[i] == ' ') {
			max_args++;
			while (cmdline[i] == ' ' && i < len) i++; // don't count "a      b   c" as more than three arguments
		}
	}

	char **argv = kmalloc((max_args + 1) * sizeof(char *));
	memset(argv, 0, (max_args + 1) * sizeof(char *));

	const char *c = cmdline;

	// To hold the current argument while parsing
	// (we don't know what size to malloc() until we're done with an argument)
	char arg[256] = {0};
	size_t ai = 0;

	len = strlen(c);
	bool quote = false;
	while (*c == ' ') c++; // skip leading spaces

	while (c < cmdline + len) {
		while ((*c > ' ' && *c != '"' && *c != 0) || (*c == ' ' && quote)) {
			// Regular text, or stuff within a quote: just copy this
			arg[ai++] = *c++;
		}

		if (*c == '"') {
			// We hit a quotemark. Change the quote status, skip the character (don't copy it), and continue
			quote = !quote;
			c++;
			if (*c != 0)
				continue; // else save before we exit the loop, just below
		}

		if ((*c == ' ' && !quote) || *c == 0) {
			// Allocate memory, save this argument, increase argc
			arg[ai++] = 0;
			argv[*argc] = kmalloc(ai);
			assert(max_args > *argc);
			strlcpy(argv[*argc], arg, ai);
			(*argc)++;
			ai = 0;
		}

		// Skip whitespace until the next argument
		while (*c == ' ') c++;
	}

	return argv;
}

static task_t *create_task_int( void (*entry_point)(void *, uint32), const char *name, console_t *console, uint8 privilege, void *data, uint32 data_len);

task_t *create_task( void (*entry_point)(void *, uint32), const char *name, console_t *con, void *data, uint32 data_len) {
	task_t *task = create_task_int(entry_point, name, con, 0 /* privilege level */, data, data_len);
	assert(task != NULL);

	assert(task->console == con);
	if (con != NULL) {
		node_t *n = list_find_last(con->tasks, (void *)task);
		if (n)
			assert((task_t *)n->data == task);
	}

	return task;
}

task_t *create_task_elf(const char *path, console_t *con, void *data, uint32 data_len) {
	assert(path != NULL);
	INTERRUPT_LOCK;

	char buf[1024] = {0};
	strlcpy(buf, path, 1024);
	path_basename(buf);

	task_t *task = create_task_int((void *)0 /* set up later on */, buf /* task name */, con, 3, data, data_len);
	task->state = TASK_IDLE; // Ensure the task doesn't start until the image is fully loaded
	assert(task != NULL);

	if (!elf_load(path, task, data)) {
		// Abort!
		task->state = TASK_EXITING;
		destroy_task(task);
		INTERRUPT_UNLOCK;
		return NULL;
	}

	// Okay, we can let it run now!
	task->state = TASK_RUNNING;

	INTERRUPT_UNLOCK;
	return task;
}

uint32 *set_task_stack(task_t *task, void *data, uint32 data_len, uint32 entry_point) {
	assert(task != NULL);
	assert(task->stack != NULL);

	/* Set up the kernel stack of the new process */
	uint32 *kernelStack = task->stack;
	uint32 code_segment = 0x08;

	/* data and data length parameters (in opposite order) */
	*(--kernelStack) = data_len;
	*(--kernelStack) = (uint32)data;

	/* Functions will call this automatically when they attempt to return */
	*(--kernelStack) = (uint32)&_exit; // TODO: what will the argument be here? Something that causes stack corruption?

	if (task->privilege == 3) {
		*(--kernelStack) = 0x23; /* SS */
		*(--kernelStack) = (USER_STACK_START - 12); /* ESP */
		code_segment = 0x1b; /* 0x18 | 3 */
	}

	*(--kernelStack) = 0x0202; /* EFLAGS: IF = 1, IOPL = 0 */
	*(--kernelStack) = code_segment;        /* CS */
	*(--kernelStack) = entry_point; // entry point / EIP
	*(--kernelStack) = 0;                   /* Error code */
	*(--kernelStack) = 0;                   /* Interrupt number */

	/* GPRs, except ESP */
	*(--kernelStack) = 0;
	*(--kernelStack) = 0;
	*(--kernelStack) = 0;
	*(--kernelStack) = 0;
	*(--kernelStack) = 0;
	*(--kernelStack) = 0;
	*(--kernelStack) = 0;

	uint32 data_segment = (task->privilege == 3) ? 0x23 : 0x10;

	/* Data segments (DS, ES, FS, GS) */
	*(--kernelStack) = data_segment;
	*(--kernelStack) = data_segment;
	*(--kernelStack) = data_segment;
	*(--kernelStack) = data_segment;

	/* Now that we're done on the stack, set the stack pointers in the task structure */
	task->esp = (uint32)kernelStack;
	task->ss = data_segment;

	return kernelStack;
}

static task_t *create_task_int( void (*entry_point)(void *, uint32), const char *name, console_t *console, uint8 privilege, void *data, uint32 data_len) {
	assert(privilege == 0 || privilege == 3);

	task_t *task = kmalloc(sizeof(task_t));
	memset(task, 0, sizeof(task_t));

	task->id = next_pid++;
	task->esp = 0;

	/*
	 * Use guard pages around the stack
	 * This wastes a "lot" of memory (16 kB total, per task of course), to keep the code simple...
	 * The heap adds a header and a footer around the allocated memory,
	 * so to get page-aligned storage with guard pages around, we need
	 * to allocate a bunch of memory extra for those full pages, AND to store
	 * the heap stuff before and after them.
	 */

	// tmp: the entire memory area, including guard pages
	uint32 tmp = (uint32)kmalloc_a(KERNEL_STACK_SIZE + 4*4096);
	// start_guard: address to the guard page BEFORE the memory (protects against OVERflow)
	uint32 start_guard = (uint32)(tmp + 4096);
	task->stack = (void*) (tmp + 2*4096 + KERNEL_STACK_SIZE);
	// end_guard: address to the guard page AFTER the memory (protects against UNDERflow)
	uint32 end_guard = (uint32)task->stack;

	assert(tmp + KERNEL_STACK_SIZE + 2*4096 == (uint32)task->stack);
	assert((uint32)task->stack + 2*4096 == tmp + KERNEL_STACK_SIZE + 4*4096);

	/* Zero the stack, so user applications can't peer in to what may have been on the kernel heap */
	memset((void *)tmp, 0, KERNEL_STACK_SIZE + 4*4096);

	/* Set the guard pages */
	vmm_set_guard(start_guard, kernel_directory);
	vmm_set_guard(end_guard,   kernel_directory);

	task->privilege = privilege;

	if (current_task && current_task->pwd)
		task->pwd = strdup(current_task->pwd);
	else
		task->pwd = strdup("/");

	if (task->privilege == 0) {
		strcpy(task->name, "[");
		strlcat(task->name, name, TASK_NAME_LEN - 1);
		strlcat(task->name, "]", TASK_NAME_LEN);
	}
	else
		strlcpy(task->name, name, TASK_NAME_LEN);

	// Set up the task's file descriptor table
	task->fdtable = kmalloc(sizeof(struct open_file) * MAX_OPEN_FILES); // TODO: smaller size + dynamic resizing
	memset(task->fdtable, 0, sizeof(struct open_file) * MAX_OPEN_FILES);

	task->parent = NULL;

	if (task->privilege == 3) {
		task->mm = vmm_create_user_mm();
		task->old_mm = NULL;

		// Set up stdin, stdout and stderr for this task
		// Note: The entire table was just zeroed (above)
		struct open_file *stdin = kmalloc(sizeof(struct open_file));
		memset(stdin, 0, sizeof(struct open_file));
		stdin->dev = (dev_t)-1;
		stdin->count = 1;
		stdin->fops.read  = stdio_read;
		stdin->fops.write = stdio_write;
		stdin->fops.close = stdio_close;
		stdin->fops.fstat = stdio_fstat;

		task->fdtable[0] = stdin;

		// Copy this info for stdout and stderr
		// Differences are handled in the IO functions
		struct open_file *stdout = kmalloc(sizeof(struct open_file));
		struct open_file *stderr = kmalloc(sizeof(struct open_file));
		memcpy(stdout, stdin, sizeof(struct open_file));
		memcpy(stderr, stdin, sizeof(struct open_file));
		task->fdtable[1] = stdout;
		task->fdtable[2] = stderr;
	}
	else if (task->privilege == 0) {
		task->mm = vmm_create_kernel_mm();
	}
	else
		panic("Task privilege isn't 0 or 3!");

	task->children = list_create();

	/* All tasks are running by default */
	task->state = TASK_RUNNING;
	task->wakeup_time = 0;

	/* Set up a console for the new task */
	task->console = console;
	if (console)
		list_append(task->console->tasks, task);

	set_task_stack(task, data, data_len, (uint32)entry_point);

	/* Add the new task in the ready queue; we'll insert it so it runs next */
	list_node_insert_after(list_find_first((list_t *)&ready_queue, (void *)current_task), (void *)task);

	/* Switch to the new console */
	//if (console)
		//console_switch(task->console);

	return task;
}

int fork(void) {
	assert(current_task->privilege == 3);

	uint32 esp;
	asm volatile("mov %%esp, %[esp]" : [esp]"=a"(esp) : : "cc", "memory");

	registers_t *regs = (registers_t *)((uint32)current_task->stack - sizeof(registers_t));

	task_t *parent = (task_t *)current_task;
	task_t *child = kmalloc(sizeof(task_t));
	memset(child, 0, sizeof(task_t));

	child->id = next_pid++;
	child->esp = 0;

	/*
	 * Use guard pages around the stack
	 * This wastes a "lot" of memory (16 kB total, per task of course), to keep the code simple...
	 * The heap adds a header and a footer around the allocated memory,
	 * so to get page-aligned storage with guard pages around, we need
	 * to allocate a bunch of memory extra for those full pages, AND to store
	 * the heap stuff before and after them.
	 */

	// tmp: the entire memory area, including guard pages
	uint32 tmp = (uint32)kmalloc_a(KERNEL_STACK_SIZE + 4*4096);
	// start_guard: address to the guard page BEFORE the memory (protects against OVERflow)
	uint32 start_guard = (uint32)(tmp + 4096);
	child->stack = (void*) (tmp + 2*4096 + KERNEL_STACK_SIZE);
	// end_guard: address to the guard page AFTER the memory (protects against UNDERflow)
	uint32 end_guard = (uint32)child->stack;

	assert(tmp + KERNEL_STACK_SIZE + 2*4096 == (uint32)child->stack);
	assert((uint32)child->stack + 2*4096 == tmp + KERNEL_STACK_SIZE + 4*4096);

	/* Zero the stack, so user applications can't peer in to what may have been on the kernel heap */
	memset((void *)tmp, 0, KERNEL_STACK_SIZE + 4*4096);

	/* Set the guard pages */
	// TODO: why kernel_directory? Was that on purpose?
	vmm_set_guard(start_guard, kernel_directory);
	vmm_set_guard(end_guard,   kernel_directory);

	child->privilege = 3;

	assert(current_task->pwd != NULL);
	assert(current_task->pwd[0] != 0);
	child->pwd = strdup(current_task->pwd);

	strlcpy(child->name, parent->name, TASK_NAME_LEN);

	// Set up the memory map struct for this task
	// This is arguably the main step fork performs.
	child->mm = vmm_clone_mm(parent->mm);

	// Set up the task's file descriptor table
	child->fdtable = kmalloc(sizeof(struct open_file) * MAX_OPEN_FILES);
	memcpy(child->fdtable, parent->fdtable, sizeof(struct open_file) * MAX_OPEN_FILES);
	for (int i = 0; i < MAX_OPEN_FILES; i++) {
		if (child->fdtable[i] != NULL) {
			// Since we only copied the pointers, this increases for the parent as well, of course
			assert(parent->fdtable[i]->count > 0);
			child->fdtable[i]->count++;
		}
	}

	// Keep track of the tasks
	assert(parent->children != NULL);
	list_append(parent->children, child);
	child->parent = parent;
	child->children = list_create();

	child->state = TASK_IDLE;
	child->wakeup_time = 0;

	/* Set up a console for the new task */
	child->console = parent->console;
	if (child->console) {
		list_append(child->console->tasks, child);
	}

	/* Set up the kernel stack of the new process */
	uint32 *kernelStack = child->stack;

	/* data and data length parameters (in opposite order) */
	*(--kernelStack) = 0; //data_len;
	*(--kernelStack) = 0; // (uint32)data;

	/* Functions will call this automatically when they attempt to return */
	*(--kernelStack) = (uint32)&_exit;

	*(--kernelStack) = 0x23; /* SS */
	*(--kernelStack) = regs->useresp;
	uint32 code_segment = 0x1b; /* 0x18 | 3 */

	*(--kernelStack) = 0x0202;				/* EFLAGS: IF = 1, IOPL = 0 */
	*(--kernelStack) = code_segment;        /* CS */
	*(--kernelStack) = (uint32)regs->eip;
	*(--kernelStack) = 0;                   /* Error code */
	*(--kernelStack) = 0;                   /* Interrupt number */

	/* GPRs, except ESP */
	*(--kernelStack) = 0; //regs->eax; -- return value should be 0 for the child
	*(--kernelStack) = regs->ecx;
	*(--kernelStack) = regs->edx;
	*(--kernelStack) = regs->ebx;
	*(--kernelStack) = regs->ebp;
	*(--kernelStack) = regs->esi;
	*(--kernelStack) = regs->edi;

	uint32 data_segment = 0x23;

	/* Data segments (DS, ES, FS, GS) */
	*(--kernelStack) = data_segment;
	*(--kernelStack) = data_segment;
	*(--kernelStack) = data_segment;
	*(--kernelStack) = data_segment;

	/* Now that we're done on the stack, set the stack pointers in the task structure */
	printk("fork: kernelStack %p, child->stack %p\n", kernelStack, child->stack);
	child->esp = (uint32)kernelStack;
	child->ss = data_segment;

	/* Add the new task in the ready queue; we'll insert it so it runs next */
	list_node_insert_after(list_find_first((list_t *)&ready_queue, (void *)current_task), (void *)child);

	child->state = TASK_RUNNING;

	return child->id;
}

int sys_wait(int *status) {
	if (status != NULL && !CHECK_ACCESS_WRITE(status, sizeof(int)))
		return -EFAULT;
	if (current_task->children == NULL || current_task->children->count <= 0)
		return -ECHILD;

	// Check if we have any unwaited-for (dead) children already
	{
	INTERRUPT_LOCK;
	assert(current_task->children->count > 0);
	list_foreach(current_task->children, it) {
		task_t *child = (task_t *)it->data;
		if (child->state == TASK_DEAD) {
			// Yup, found one! Handle it.
			return do_wait_one((task_t *)current_task, child, status);
		}
	}
	INTERRUPT_UNLOCK;
	}

	// No, but we do have a child that is still alive. Wait for it.
	current_task->state = TASK_WAITING;
	YIELD;

	// We should ONLY get here when _exit() has finished on at least one child.
	{
	INTERRUPT_LOCK;
	assert(current_task->children->count > 0);
	list_foreach(current_task->children, it) {
		task_t *child = (task_t *)it->data;
		if (child->state == TASK_DEAD) {
			// Yup, found one! Handle it.
			return do_wait_one((task_t *)current_task, child, status);
		}
	}
	INTERRUPT_UNLOCK;
	}
	panic("wait(): task was scheduled with no dead child!");
	return 0; // To silence the compiler
}

pid_t sys_waitpid(pid_t pid, int *status, int options) {
	if (status != NULL && !CHECK_ACCESS_WRITE(status, sizeof(int)))
		return -EFAULT;
	if (current_task->children == NULL || current_task->children->count <= 0)
		return -ECHILD;
	if (options != 0 && options != WNOHANG && options != (WNOHANG | WUNTRACED))
		return -EINVAL;

	if (pid == -1 && options == 0)
		return sys_wait(status);
	else if (pid < -1 || pid == 0) {
		panic("waitpid() with pid < -1, or pid == 0: process groups are not implemented!");
		return -ENOSYS;
	}
	assert(pid > 0 || pid == -1);

	// Check if we have any unwaited-for (dead) children already
	{
		bool child_found = false;
		INTERRUPT_LOCK;
		assert(current_task->children->count > 0);
		list_foreach(current_task->children, it) {
			task_t *child = (task_t *)it->data;
			if (pid == -1 || child->id == pid) {
				child_found = true;
				if (child->state == TASK_DEAD) {
					return do_wait_one((task_t *)current_task, child, status);
				}
			}
		}
		INTERRUPT_UNLOCK;

		if (!child_found) {
			// The child the caller wants to wait for doesn't exist / isn't a child of this task!
			return -ECHILD;
		}
	}

	// No, but we do have a child that is still alive.
	// Should we wait for it?
	if (options & WNOHANG) {
		// No!
		return 0;
	}

	// Otherwise, yes: wait for it.
	while (true) {
		current_task->state = TASK_WAITING;
		YIELD;

		// We should ONLY get here when _exit() has finished on at least one child.
		INTERRUPT_LOCK;
		assert(current_task->children->count > 0);
		list_foreach(current_task->children, it) {
			task_t *child = (task_t *)it->data;
			if (child->state == TASK_DEAD && (child->id == pid || pid == -1)) {
				// Yup, found it! Handle this one.
				return do_wait_one((task_t *)current_task, child, status);
			}
		}
		INTERRUPT_UNLOCK;
	}
	assert(false); // This should never be reached

	return 0; // To silence the compiler
}

void set_entry_point(task_t *task, uint32 addr) {
	assert(task != NULL);
	*((uint32 *)((uint32)task->stack - 32)) = addr;
}

/* Puts the calling process into the IOWAIT state, and returns to it(!). NOT called from ISRs. */
void scheduler_set_iowait(void) {
	current_task->state = TASK_IOWAIT;
	/* TODO: set a timeout? */
}

static bool task_iowait_predicate(node_t *node) {
	assert(node != NULL);
	assert(node->data != NULL);
	task_t *t = (task_t *)node->data;
	return (t->state == TASK_IOWAIT);
}

/* Finds the (TODO! shouldn't be singular) IOWAIT process and wakes it, i.e. switches to it.
 * This function *IS* called from ISRs. */
uint32 scheduler_wake_iowait(uint32 esp) {
	//assert(current_task->state != TASK_IOWAIT);

	node_t *n = NULL;
	if (current_task->state != TASK_IOWAIT) {
		if (current_task != &kernel_task && kernel_task.state == TASK_IOWAIT)
			n = ready_queue.head;
		else {
			/* Only do this if the CURRENT task isn't the task to "wake" */
			n = list_node_find_next_predicate(ready_queue.head, task_iowait_predicate);
			assert(n != NULL);
		}
	}

	/* make sure this is the ONLY process in IOWAIT */
	/* This needs work! */
	if (n == NULL)
		n = list_find_first((list_t *)&ready_queue, (void *)current_task);
	if (n != NULL)
		assert(list_node_find_next_predicate(n, task_iowait_predicate) == NULL);

	/* Wake the task up, and switch to it! */
	task_t *iotask = (task_t *)n->data;
	assert(iotask->state == TASK_IOWAIT);

	iotask->state = TASK_RUNNING;

	return switch_task(iotask, esp);
}

uint32 switch_task(task_t *new_task, uint32 esp) {
	assert(interrupts_enabled() == false);
	if (current_task == NULL || task_switching == false) {
		panic("switch_task with no current_task or task_switching disabled");
		// return 0;
	}

	assert(new_task->state != TASK_SLEEPING);

	if (new_task == current_task)
		return esp;

	if (new_task->state == TASK_WAKING_UP)
		new_task->state = TASK_RUNNING;

	/* this should really be a no-op, since, interrupts should already be disabled from the ISR. */
	//disable_interrupts();
	//task_switching = false;

	if (new_task->mm->page_directory != current_task->mm->page_directory)
		switch_page_directory(new_task->mm->page_directory);

	/* Store the current ESP */
	current_task->esp = esp;

	current_task = new_task;
	console_task = new_task;

	/* Update the TSS */
	assert(current_task->stack != NULL);
	assert(current_task->esp != 0);
	assert(current_task->ss == 0x10 || current_task->ss == 0x23);
	tss_switch((uint32)current_task->stack, current_task->esp, current_task->ss);

	//task_switching = true;
	//enable_interrupts(); // let the ISR do this

	/*
	 * Return the ESP of the new task (which is now set as current_task).
	 * The next line of code to execute is the one in the ISR that updates the ESP register to this value.
	 * After that, the ISR pops off all the registers etc. and continues execution at the EIP found
	 * at this ESP value. Since all of those belong to the new task, we will have switched tasks.
	 */
	return current_task->esp;
}

static bool task_running_predicate(node_t *node) {
	assert(node != NULL);
	assert(node->data != NULL);
	task_t *t = (task_t *)node->data;
	return (t->state & TASK_RUNNING);
}

void set_next_task(task_t *task) {
	next_task = task;
	next_task->state = TASK_RUNNING;
	next_task->wakeup_time = 0;
}

/* This function is called by the IRQ handler whenever the timer fires (or a software interrupt 0x7e is sent). */
uint32 scheduler_taskSwitch(uint32 esp) {
	assert(interrupts_enabled() == false);
	if (task_switching == false || (current_task == &kernel_task && ready_queue.count == 1))
		return esp;

	// If next_task is set, use it.
	// Set by e.g. the network handler to speed things up.
	// Not very good for security (DDoS etc.), but that's irrelevant at the moment.
	if (next_task) {
		task_t *tmp = next_task;
		next_task = NULL;
		if (tmp->state & TASK_SLEEPING) {
			tmp->state = TASK_RUNNING;
			tmp->wakeup_time = 0;
		}

		return switch_task(tmp, esp);
	}
	
	if (current_task->did_execve) {
		current_task->did_execve = false;
		esp = current_task->esp;
		return esp;
	}

	/*
	 * Look through the list of tasks to find sleeping tasks; if
	 * any are found, check whether they should be woken up now.
	 */
	const uint32 ticks = gettickcount(); /* fetch just the once; interrupts are disabled, so the tick count can't change */
	list_foreach_dot(ready_queue, it) {
		task_t *p = (task_t *)it->data;
		if (p->state == TASK_SLEEPING && p->wakeup_time <= ticks) {
			/* Wake this task! */
			p->wakeup_time = 0;
			p->state = TASK_WAKING_UP;
			//return switch_task(p, esp); // It turns out that this will cause some tasks to never run...
		}
	}

	node_t *old_task_node = list_find_first((list_t *)&ready_queue, (void *)current_task);

	task_t *old_task = (task_t *)current_task;
	task_t *new_task = NULL;

	if (old_task_node == NULL) {
		/* The "current task" is not on the run queue. This would happen if
		 * it had just been killed. Start over from the beginning of the run queue,
		 * since we don't know which task would've been the next one.  */
		new_task = (task_t *)(ready_queue.head->data);
	}
	else {
		/* Find the next task to run (exclude tasks that are sleeping, in IOWAIT, exiting, etc.) */
		node_t *new_task_node = list_node_find_next_predicate(old_task_node, task_running_predicate);

		if (new_task_node == NULL) {
			/* all tasks are asleep, possibly except for the current one! */
			if ( ((task_t *)old_task_node->data)->state & TASK_RUNNING) {
				/* only the current process is not sleeping; let's not switch, then! */
				return (esp);
			}
			else {
				// There is NOTHING to run. Let's run the HLT task.
				return switch_task(idle_task, esp);
			}
		}

		new_task = (task_t *)new_task_node->data;
	}

	assert(new_task != NULL);

	/* new_task now points towards the task we want to run */

    if (old_task == new_task) {
		/* no point in switching, eh? */
        return(esp);
	}

	/* Looks like we found a running task to switch to! Let's do so. */

	assert(new_task->state & TASK_RUNNING);

    return switch_task(new_task, esp);
}

int getpid(void) {
	return current_task->id;
}

int getppid(void) {
	assert(current_task->privilege == 3);
	assert(current_task->parent != NULL);
	return current_task->parent->id;
}

/* Takes a task off the run queue until enough time has passed */
void sleep(uint32 milliseconds) {
	if (milliseconds == 0)
		return;

	const uint32 start_ticks = gettickcount();
	uint32 ticks_to_wait = milliseconds / (1000 / TIMER_HZ);

	/* Wait a minimum of 1 tick; if the caller called sleep() with a nonzero argument, they
	 * may not be happy with an instant return. */
	if (ticks_to_wait == 0)
		ticks_to_wait = 1;

	/* Sanity checks */
	assert(current_task->state != TASK_SLEEPING);
	assert(current_task->state != TASK_IOWAIT);
	assert(current_task->wakeup_time == 0);
	//assert(current_task != &kernel_task);

	/* Mark the task as sleeping */
	current_task->state = TASK_SLEEPING;
	current_task->wakeup_time = start_ticks + ticks_to_wait;

	/* Force a task switch */
	YIELD;
}

int sys_nanosleep(const struct timespec *rqtp, struct timespec *rmtp __attribute__((unused))) {
	if (rqtp == NULL || !CHECK_ACCESS_READ(rqtp, sizeof(struct timespec)))
		return -EFAULT;

	uint32 ms = (rqtp->tv_sec) * 1000 + ((rqtp->tv_nsec) / 1000000);
	sleep(ms);

	return 0;
}
