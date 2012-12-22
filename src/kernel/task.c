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

uint32 next_pid = 2; /* kernel_task has PID 1 */

task_t kernel_task = {
	.id = 1,
	.esp = 0,
	.ss = 0x10,
	.stack = 0, /* set later */
	.page_directory = 0,
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

void destroy_task(task_t *task) {
	assert(task != &kernel_task);
	assert(task != current_task);
	assert(task->state == TASK_EXITING);

	//task_switching = false;

	if (task->console != NULL) {
		/* Remove this task from the console chain */
		list_remove(task->console->tasks, list_find_first(task->console->tasks, (void *)task));

		/*
		console_t *prev = task->console->prev_console;
		while (prev != NULL) {
			node_t *task_ptr = list_find_first(prev->tasks, (void *)task);
			if (task_ptr != NULL)
				list_remove(prev->tasks, task_ptr);
			prev = prev->prev_console;
		}
		*/

		/* Switch from (if necessary) and destroy this task's console */
		//console_destroy(task->console);
	}

	if (task->privilege == 3) {
		// Free all of this task's frames (user space stack, stuff loaded from ELF files, etc.)
		assert(task->mm != NULL);
		assert(task->mm->pages != NULL);

		for (node_t *it = task->mm->pages->head; it != NULL; it = it->next) {
			addr_entry_t *entry = (addr_entry_t *)it->data;
			for (uint32 addr = (uint32)entry->start; addr < (uint32)entry->start + entry->num_pages * PAGE_SIZE; addr += PAGE_SIZE) {
				vmm_free(addr, task->page_directory);
			}
			kfree(entry);
		}

		// Free stuff in the file descriptor table (the table itself is in struct task)
		for (int i=0; i < MAX_OPEN_FILES; i++) {
			if (task->fdtable[i].path)
				kfree(task->fdtable[i].path);
		}

		assert(task->heap != NULL);
		heap_destroy(task->heap, task->page_directory);

		list_remove(pagedirs, list_find_first(pagedirs, task->page_directory));
		destroy_user_page_dir(task->page_directory);

		list_destroy(task->mm->pages);
		kfree(task->mm);
		task->mm = NULL;
	}

	/* Delete this task from the queue */
	list_remove((list_t *)&ready_queue, list_find_first((list_t *)&ready_queue, (void *)task));

	/* Free the kernel stack, after re-mapping the guard pages again */
	vmm_clear_guard((uint32)task->stack - KERNEL_STACK_SIZE - 4096, kernel_directory);
	vmm_clear_guard((uint32)task->stack, kernel_directory);
	kfree((void *)( (uint32)task->stack - KERNEL_STACK_SIZE - 2*4096 ));

	kfree(task);
}


bool kill_pid(int pid) {
	/* Kills the task with a certain PID */
	assert(pid != 1); /* kernel_task has PID 1 */

	INTERRUPT_LOCK;

	for (node_t *it = ready_queue.head; it != NULL; it = it->next) {
		task_t *t = (task_t *)it->data;
		if (t->id == pid) {
			t->state = TASK_EXITING;
			INTERRUPT_UNLOCK;
			return true;
		}
	}

	INTERRUPT_UNLOCK;
	return false;
}

void kill(task_t *task) {
	task->state = TASK_EXITING;
}

void _exit(void) {
	kill((task_t *)current_task);
	YIELD;
	panic("this should never be reached (in _exit after switching tasks)");
}

void user_exit(void) {
	asm volatile("int $0x80" : : "a"(0 /* _exit syscall number */));
}

void idle_task_func(void *data, uint32 length) {
	while (true) {
		asm volatile("hlt");
	}
}

void init_tasking(uint32 kerntask_esp0) {
	disable_interrupts();

	kernel_task.page_directory = kernel_directory;
	kernel_task.stack = (void *)kerntask_esp0;
	strlcpy(kernel_task.name, "[kernel_task]", TASK_NAME_LEN);

	idle_task = create_task(&idle_task_func, "idle_task", NULL, NULL, 0);
	idle_task->state = TASK_IDLE; /* TODO(?): should really be ->priority, but there is no such thing yet */

	task_switching = true;
	enable_interrupts();
}

static char **parse_command_line(const char *cmdline, uint32 *argc, task_t *task) {
	// Count the worst-case number of arguments. There may be fewer due to quoting
	uint32 max_args = 1; // the command name is always there
	size_t len = strlen(cmdline);
	for (size_t i=0; i < len; i++) {
		if (cmdline[i] == ' ') {
			max_args++;
			while (cmdline[i] == ' ' && i < len) i++; // don't count "a      b   c" as more than three arguments
		}
	}

	// We use heap_alloc here to specify the heap, since malloc() uses current_task->heap;
	// current_task is still set to the parent task

	char **argv = heap_alloc((max_args + 1) * sizeof(char *), false, task->heap);
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
			argv[*argc] = heap_alloc(ai, false, task->heap);
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

	task_t *task = create_task_user((void *)0 /* set up later on */, buf /* task name */, con, data, data_len);
	task->state = TASK_IDLE; // Ensure the task doesn't start until the image is fully loaded
	assert (task != NULL);

	if (!elf_load(path, task)) {
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

task_t *create_task_user( void (*entry_point)(void *, uint32), const char *name, console_t *con, void *data, uint32 length) {
	INTERRUPT_LOCK;

	task_t *task = create_task_int(entry_point, name, con, 3 /* privilege level */, data, length);
	assert(task != NULL);

	assert(task->console == con);
	if (con != NULL) {
		node_t *n = list_find_last(con->tasks, (void *)task);
		if (n)
			assert((task_t *)n->data == task);
	}

	INTERRUPT_UNLOCK;

	return task;
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

	if (task->privilege == 3) {
		task->page_directory = create_user_page_dir(); /* clones the kernel structures */

		// Set up the memory map struct for this task
		task->mm = kmalloc(sizeof(struct task_mm));
		memset(task->mm, 0, sizeof(struct task_mm));
		task->mm->brk_start = 0;
		task->mm->brk = 0;
		task->mm->pages = list_create();

		/* Set up a usermode stack for this task */
		vmm_alloc_user(USER_STACK_START - (USER_STACK_SIZE + PAGE_SIZE), USER_STACK_START + PAGE_SIZE, task->page_directory, PAGE_RW);

		/* Set a guard page */
		vmm_set_guard(USER_STACK_START - (USER_STACK_SIZE + PAGE_SIZE), task->page_directory);

		// Write down the above
		addr_entry_t *entry = kmalloc(sizeof(addr_entry_t));
		entry->start = (void *)(USER_STACK_START - USER_STACK_SIZE - PAGE_SIZE);
		entry->num_pages = ((USER_STACK_START + PAGE_SIZE) - ((uint32)entry->start)) / PAGE_SIZE;
		list_append(task->mm->pages, entry);

		// Pass command line arguments to the task
		assert(current_directory == kernel_directory);
		switch_page_directory(task->page_directory);
		//*((uint32 *)(USER_STACK_START - 4)) = (uint32)&user_exit;

		task->heap = heap_create(USER_HEAP_START, USER_HEAP_INITIAL_SIZE, USER_HEAP_MAX_ADDR, 0, 0, task->page_directory); // not supervisor, not read-only

		// Parse the data into argv/argc
		uint32 argc = 0;
		char **argv = NULL;
		argv = parse_command_line(data, &argc, task);
		assert(argv != NULL);
		assert(argv[0] != NULL);
		assert(strlen(argv[0]) > 0);

		*((uint32 *)(USER_STACK_START - 4)) = (uint32)argv;
		*((uint32 *)(USER_STACK_START - 8)) = (uint32)argc;

		switch_page_directory(kernel_directory);

		// Clear the task's file descriptor table
		memset(task->fdtable, 0, sizeof(struct open_file) * MAX_OPEN_FILES);

		// Set up stdin, stdout and stderr for this task
		// Note: The entire table was just zeroed (above)
		struct open_file *f = (struct open_file *)&task->fdtable[0];
		f->dev = (dev_t)-1;
		f->count = 1;
		f->fops.read  = stdio_read;
		f->fops.write = stdio_write;
		f->fops.close = stdio_close;
		f->fops.fstat = stdio_fstat;

		// Copy this info for stdout and stderr
		// Differences are handled in the IO functions
		memcpy(&task->fdtable[1], f, sizeof(struct open_file));
		memcpy(&task->fdtable[2], f, sizeof(struct open_file));
	}
	else if (task->privilege == 0) {
		task->page_directory = current_directory;
		task->mm = NULL;
	}
	else
		panic("Task privilege isn't 0 or 3!");

	/* All tasks are running by default */
	task->state = TASK_RUNNING;
	task->wakeup_time = 0;

	/* Set up a console for the new task */
	task->console = console;
	if (console) {
		list_append(task->console->tasks, task);

		/* TODO: this is not a great solution. Adds this task to the previous console's task list */
		/*
		console_t *prev = task->console->prev_console;
		while (prev != NULL) {
			list_append(prev->tasks, task);
			prev = prev->prev_console;
		}
		*/
	}

	/* Set up the kernel stack of the new process */
	uint32 *kernelStack = task->stack;
	uint32 code_segment = 0x08;

	/* data and data length parameters (in opposite order) */
	*(--kernelStack) = data_len;
	*(--kernelStack) = (uint32)data;

	/* Functions will call this automatically when they attempt to return */
	*(--kernelStack) = (uint32)&_exit;

	if (task->privilege == 3) {
		*(--kernelStack) = 0x23; /* SS */
		*(--kernelStack) = (USER_STACK_START - 12); /* ESP */
		code_segment = 0x1b; /* 0x18 | 3 */
	}

	*(--kernelStack) = 0x0202; /* EFLAGS: IF = 1, IOPL = 0 */
	*(--kernelStack) = code_segment;        /* CS */
	*(--kernelStack) = (uint32)entry_point; /* EIP */
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

	uint32 data_segment = (privilege == 3) ? 0x23 : 0x10;

	/* Data segments (DS, ES, FS, GS) */
	*(--kernelStack) = data_segment;
	*(--kernelStack) = data_segment;
	*(--kernelStack) = data_segment;
	*(--kernelStack) = data_segment;

	/* Now that we're done on the stack, set the stack pointers in the task structure */
	task->esp = (uint32)kernelStack;
	task->ss = data_segment;

	/* Add the new task in the ready queue; we'll insert it so it runs next */
	list_node_insert_after(list_find_first((list_t *)&ready_queue, (void *)current_task), (void *)task);

	/* Switch to the new console */
	//if (console)
		//console_switch(task->console);

	return task;
}

void set_entry_point(task_t *task, uint32 addr) {
	assert(task != NULL);
	assert(task->privilege == 3);
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

	if (new_task->page_directory != current_task->page_directory)
		switch_page_directory(new_task->page_directory);

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

	/*
	 * Look through the list of tasks to find sleeping tasks; if
	 * any are found, check whether they should be woken up now.
	 */
	const uint32 ticks = gettickcount(); /* fetch just the once; interrupts are disabled, so the tick count can't change */
	for (node_t *it = ready_queue.head; it != NULL; it = it->next) {
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
