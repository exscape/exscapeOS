#include <types.h>
#include <string.h>
#include <kernel/kernutil.h>
#include <kernel/kheap.h>
#include <kernel/vmm.h>
#include <kernel/console.h>
#include <kernel/task.h>
#include <kernel/gdt.h>
#include <kernel/timer.h>
#include <kernel/list.h>
#include <kernel/syscall.h>
#include <kernel/vfs.h>
#include <kernel/elf.h>

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
	.eip = 0,
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

void kill(task_t *task) {
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
		for (node_t *it = task->user_addr_table->head; it != NULL; it = it->next) {
			addr_entry_t *entry = (addr_entry_t *)it->data;
			for (uint32 addr = (uint32)entry->start; addr < (uint32)entry->start + entry->num_pages * PAGE_SIZE; addr += PAGE_SIZE) {
				vmm_free(addr, task->page_directory);
			}
			kfree(entry);
		}

		list_remove(pagedirs, list_find_first(pagedirs, task->page_directory));
		destroy_user_page_dir(task->page_directory);

		list_destroy(task->user_addr_table);
	}

	/* Delete this task from the queue */
	list_remove((list_t *)&ready_queue, list_find_first((list_t *)&ready_queue, (void *)task));

	/* Free the kernel stack, after re-mapping the guard pages again */
	vmm_set_guard((uint32)task->stack - KERNEL_STACK_SIZE - 4096, kernel_directory, false);
	vmm_set_guard((uint32)task->stack, kernel_directory, false);
	kfree((void *)( (uint32)task->stack - KERNEL_STACK_SIZE - 2*4096 ));

	kfree(task);

	//task_switching = true;

	/* If the task being killed is currently active, force a switch from it.
	 * The pointer is still valid, even though the memory it's pointing to is not, so we can still use it for a comparison. */
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

void exit_proc(void) {
	//kill((task_t *)current_task);
	current_task->state = TASK_EXITING; // set task to terminate
	asm volatile("int $0x7e");
	panic("this should never be reached (in exit_proc after switching tasks)");
}

void user_exit(void) {
	syscall_exit_proc();
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

	idle_task = create_task(&idle_task_func, "[idle_task]", NULL, NULL, 0);
	idle_task->state = TASK_IDLE; /* TODO(?): should really be ->priority, but there is no such thing yet */

	task_switching = true;
	enable_interrupts();
}

static task_t *create_task_int( void (*entry_point)(void *, uint32), const char *name, console_t *console, uint8 privilege, void *data, uint32 length);

task_t *create_task( void (*entry_point)(void *, uint32), const char *name, console_t *con, void *data, uint32 length) {
	task_t *task = create_task_int(entry_point, name, con, 0 /* privilege level */, data, length);
	assert(task != NULL);

	assert(task->console == con);
	if (con != NULL) {
		node_t *n = list_find_last(con->tasks, (void *)task);
		if (n)
			assert((task_t *)n->data == task);
	}

	return task;
}

task_t *create_task_elf(fs_node_t *file, console_t *con, void *data, uint32 length) {
	assert(file != NULL);
	INTERRUPT_LOCK;

	task_t *task = create_task_user((void *)0x10000000, file->name, con, data, length);
	assert (task != NULL);

	elf_load(file, fsize(file), task);

	INTERRUPT_UNLOCK;
	return task;
}

task_t *create_task_user( void (*entry_point)(void *, uint32), const char *name, console_t *con, void *data, uint32 length) {
	INTERRUPT_LOCK;

	task_t *task = create_task_int(entry_point, name, con, 3 /* privilege level */, data, length);
	assert(task != NULL);

	static int count = 0;
	printk("create_task_user run #%d\n", count + 1);
	count++;

	assert(task->console == con);
	if (con != NULL) {
		node_t *n = list_find_last(con->tasks, (void *)task);
		if (n)
			assert((task_t *)n->data == task);
	}

	INTERRUPT_UNLOCK;

	return task;
}

static task_t *create_task_int( void (*entry_point)(void *, uint32), const char *name, console_t *console, uint8 privilege, void *data, uint32 length) {
	assert(privilege == 0 || privilege == 3);

	task_t *task = kmalloc(sizeof(task_t));
	memset(task, 0, sizeof(task_t));

	task->id = next_pid++;
	task->esp = 0;
	task->eip = 0;

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

	/* Unmap the guard pages */
	vmm_set_guard(start_guard, kernel_directory, true);
	vmm_set_guard(end_guard,   kernel_directory, true);

	task->privilege = privilege;
	strlcpy(task->name, name, TASK_NAME_LEN);

	if (task->privilege == 3) {
		task->page_directory = create_user_page_dir(); /* clones the kernel structures */

		// Store a list of all virtual addresses to unmap when the task
		// is destroyed
		task->user_addr_table = list_create();

		/* Set up a usermode stack for this task */
		vmm_alloc_user(USER_STACK_START - (USER_STACK_SIZE + PAGE_SIZE), USER_STACK_START + PAGE_SIZE, task->page_directory, true /* writable */);

		/* Set a guard page */
		vmm_set_guard(USER_STACK_START - (USER_STACK_SIZE + PAGE_SIZE), task->page_directory, true /* set guard page */);

		// Write down the above
		addr_entry_t *entry = kmalloc(sizeof(addr_entry_t));
		entry->start = (void *)(USER_STACK_START - USER_STACK_SIZE - PAGE_SIZE);
		entry->num_pages = ((USER_STACK_START + PAGE_SIZE) - ((uint32)entry->start)) / PAGE_SIZE;
		list_append(task->user_addr_table, entry);

		/* Force a call to user_exit() if the task attempts to read and RET "past" the stack */
		assert(current_directory == kernel_directory);
		switch_page_directory(task->page_directory);
		*((uint32 *)(USER_STACK_START - 4)) = (uint32)&user_exit;
		switch_page_directory(kernel_directory);
	}
	else if (task->privilege == 0) {
		task->page_directory = current_directory;
		task->user_addr_table = NULL;
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
	*(--kernelStack) = length;
	*(--kernelStack) = (uint32)data;

	/* Functions will call this automatically when they attempt to return */
	*(--kernelStack) = (uint32)&exit_proc;

	if (task->privilege == 3) {
		*(--kernelStack) = 0x23; /* SS */
		*(--kernelStack) = (USER_STACK_START - 4); /* ESP */
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
	if (current_task == NULL || task_switching == false) {
		panic("switch_task with no current_task or task_switching disabled");
		// return 0;
	}
	if (new_task == current_task)
		return esp;

	assert(new_task->state != TASK_SLEEPING);

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
	asm volatile("int $0x7e");
}
