#include <types.h>
#include <string.h>
#include <kernel/kernutil.h>
#include <kernel/kheap.h>
#include <kernel/paging.h>
#include <kernel/console.h>
#include <kernel/task.h>
#include <kernel/gdt.h>
#include <kernel/timer.h>
#include <kernel/list.h>

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

/* Externs from paging.c */
extern page_directory_t *kernel_directory;
extern page_directory_t *current_directory;
extern void alloc_frame_to_page(page_t *, bool, bool);

volatile bool task_switching = false;

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
};

/* true if the task exists and is running/sleeping; false if it has exited (or never even existed) */
bool does_task_exist(task_t *task) {
	return (list_find_first((list_t *)&ready_queue, (void *)task) != NULL);
}

void kill(task_t *task) {
	assert(task != &kernel_task);
	task_switching = false;

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

	/* TODO: destroy user page directory */

	/* Delete this task from the queue */
	list_remove((list_t *)&ready_queue, list_find_first((list_t *)&ready_queue, (void *)task));

	kfree((void *)(  (uint32)task->stack - KERNEL_STACK_SIZE ) );
	kfree(task);

	task_switching = true;

	/* If the task being killed is currently active, force a switch from it.
	 * The pointer is still valid, even though the memory it's pointing to is not, so we can still use it for a comparison. */
	if (task == current_task) {
		/* Force a task switch */
		asm volatile("int $0x7E");
		panic("Should not be reached (kill() after forcing a task switch from the killed task)");
	}
}

bool kill_pid(int pid) {
	/* Kills the task with a certain PID */
	assert(pid != 1); /* kernel_task has PID 1 */

	/* TODO: add a list_find function that uses a user-supplied comparison function? */

	for (node_t *it = ready_queue.head; it != NULL; it = it->next) {
		if ( ((task_t *)it->data)->id == pid) {
			kill((task_t *)it->data);
			return true;
		}
	}

	return false;
}

void exit_proc(void) {
	kill((task_t *)current_task);
}

void init_tasking(uint32 kerntask_esp0) {
	disable_interrupts();

	kernel_task.page_directory = kernel_directory;
	kernel_task.stack = (void *)kerntask_esp0;
	strlcpy(kernel_task.name, "kernel_task", TASK_NAME_LEN);

	task_switching = true;
	enable_interrupts();
}

static task_t *create_task_int( void (*entry_point)(void *, uint32), const char *name, console_t *console, uint8 privilege, void *data, uint32 length);

task_t *create_task( void (*entry_point)(void *, uint32), const char *name, console_t *con, void *data, uint32 length) {
	task_t *task = create_task_int(entry_point, name, con, 0 /* privilege level */, data, length);
	assert(task != NULL);

	assert(task->console == con);
	if (con != NULL)
		assert( (task_t *)list_find_last(con->tasks, (void *)task)->data == task );

	return task;
}

task_t *create_task_user( void (*entry_point)(void *, uint32), const char *name, console_t *con, void *data, uint32 length) {
	task_t *task = create_task_int(entry_point, name, con, 3 /* privilege level */, data, length);
	assert(task != NULL);

	assert(task->console == con);
	if (con != NULL)
		assert( (task_t *)list_find_last(con->tasks, (void *)task)->data == task );

	return task;
}

static task_t *create_task_int( void (*entry_point)(void *, uint32), const char *name, console_t *console, uint8 privilege, void *data, uint32 length) {
	disable_interrupts(); /* not sure if this is needed */
	task_switching = false;

	assert(privilege == 0 || privilege == 3);

	task_t *task = kmalloc(sizeof(task_t));
	memset(task, 0, sizeof(task_t));

	task->id = next_pid++;
	task->esp = 0;
	task->eip = 0;
	task->stack = (void *)( (uint32)kmalloc_a(KERNEL_STACK_SIZE) + KERNEL_STACK_SIZE );

	/* Zero the stack, so user applications can't peer in to what may have been on the kernel heap */
	memset((void *)( (uint32)task->stack - KERNEL_STACK_SIZE ), 0, KERNEL_STACK_SIZE);

	if (privilege == 0)
		task->page_directory = current_directory;
	else {
		task->page_directory = create_user_page_dir(); /* clones the kernel structures */
	}

	task->privilege = privilege;
	strlcpy(task->name, name, TASK_NAME_LEN);

#define USER_STACK_START 0xf0000000
#define USER_STACK_SIZE 32768 /* overkill? */

	if (task->privilege == 3) {
		/* Set up a usermode stack for this task */
		// void alloc_frame(uint32 virtual_addr, page_directory_t *page_dir, bool kernelmode, bool writable) {
		uint32 addr = USER_STACK_START;
		for (; addr > USER_STACK_START - USER_STACK_SIZE; addr -= 0x1000) {
			alloc_frame(addr, task->page_directory, /* kernelmode = */ false, /* writable = */ true);
		}
		/* allocate a guard page - is this really necessary? */
		alloc_frame(addr, task->page_directory, true, false);
	}

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
		*(--kernelStack) = USER_STACK_START; /* ESP */
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

	task_switching = true;
	enable_interrupts(); /* not sure if this is needed */
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

	/* this should really be a no-op, since, interrupts should already be disabled from the ISR. */
	disable_interrupts();
	task_switching = false;

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

	task_switching = true;
	//enable_interrupts(); // let the ISR do this

	/*
	 * Return the ESP of the new task (which is now set as current_task).
	 * The next line of code to execute is the one in the ISR that updates the ESP register to this value.
	 * After that, the ISR pops off all the registers etc. and continues execution at the EIP found
	 * at this ESP value. Since all of those belong to the new task, we will have switched tasks.
	 */
	return current_task->esp;
}

static bool task_not_sleeping_predicate(node_t *node) {
	assert(node != NULL);
	assert(node->data != NULL);
	task_t *t = (task_t *)node->data;
	return (t->state != TASK_SLEEPING && t->state != TASK_IOWAIT);
}

/* This function is called by the IRQ handler whenever the timer fires (or a software interrupt 0x7e is sent). */
uint32 scheduler_taskSwitch(uint32 esp) {
	if (task_switching == false || (current_task == &kernel_task && ready_queue.count == 1))
		return esp;

	/* Look through the list of tasks to find sleeping tasks; if
	 * any are found, check whether they should be woken up now.
	 * In the event that we find multiple tasks that should be woken this
	 * very instant, wake the first one, and let the next be woken on the
	 * next call to the scheduler. */
	const uint32 ticks = gettickcount(); /* fetch just the once; interrupts are disabled, so the tick count can't change */
	for (node_t *it = ready_queue.head; it != NULL; it = it->next) {
		task_t *p = (task_t *)it->data;
		if (p->state == TASK_SLEEPING && p->wakeup_time <= ticks) {
			/* Wake this task! */
			p->wakeup_time = 0;
			p->state = TASK_RUNNING;
			return switch_task(p, esp);
		}
	}

	/* We didn't find any sleeping tasks to wake right now; let's focus on switching tasks as usual instead */
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
		/* Find the next task to run (exclude sleeping tasks) */
		node_t *new_task_node = list_node_find_next_predicate(old_task_node, task_not_sleeping_predicate);

		if (new_task_node == NULL) {
			/* all tasks are asleep, possibly except for the current one! */
			if ( ((task_t *)old_task_node->data)->state == TASK_RUNNING) {
				/* only the current process is not sleeping; let's not switch, then! */
				return (esp);
			}
			else {
				panic("No running tasks found! TODO: run a HLT task here");
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

	assert(new_task->state == TASK_RUNNING);

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
	assert(current_task != &kernel_task);

	/* Mark the task as sleeping */
	current_task->state = TASK_SLEEPING;
	current_task->wakeup_time = start_ticks + ticks_to_wait;

	/* Force a task switch */
	asm volatile("int $0x7e");
}
