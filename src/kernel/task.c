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

/* timer.c */
extern const uint16 TIMER_HZ;

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
volatile task_t *current_task = &kernel_task; // the currently running task

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

/* true if the function exists and is running/sleeping; false if it has exited (or never ever existed) */
bool does_task_exist(task_t *task) {
	return (list_find_first((list_t *)&ready_queue, (void *)task) != NULL);
}

void kill(task_t *task) {
	assert(task != &kernel_task);
	task_switching = false;

	/* Switch from (if necessary) and destroy this task's console */
	console_destroy(task->console);

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

static task_t *create_task_int( void (*entry_point)(void), const char *name, console_t *console);

task_t *create_task( void (*entry_point)(void), const char *name) {
	console_t *con = console_create();
	assert(con != NULL);
	task_t *task = create_task_int(entry_point, name, con);
	assert(task != NULL);

	assert(task->console == con);
	assert(con->task == task);
	//task->console = con;
	//con->task = task;

	return task;
}

static task_t *create_task_int( void (*entry_point)(void), const char *name, console_t *console) {
	disable_interrupts(); /* not sure if this is needed */
	task_switching = false;

	task_t *task = kmalloc(sizeof(task_t));
	memset(task, 0, sizeof(task_t));

	task->id = next_pid++;
	task->esp = 0;
	task->eip = 0;
	task->stack = (void *)( (uint32)kmalloc_a(KERNEL_STACK_SIZE) + KERNEL_STACK_SIZE );
	task->page_directory = current_directory;
	strlcpy(task->name, name, TASK_NAME_LEN);

	/* All tasks are running by default */
	task->state = TASK_RUNNING;
	task->wakeup_time = 0;

	/* Set up a console for the new task */
	task->console = console;
	task->console->task = task;

	/* Set up the kernel stack of the new process */
	uint32 *kernelStack = task->stack;
	uint32 code_segment = 0x08;

	/* Functions will call this automatically when they attempt to return */
	*(--kernelStack) = (uint32)&exit_proc;

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

	uint32 data_segment = 0x10;

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
	console_switch(task->console);

	task_switching = true;
	enable_interrupts(); /* not sure if this is needed */
	return task;
}

uint32 switch_task(task_t *new_task) {
	if (current_task == NULL || task_switching == false) {
		panic("switch_task with no current_task or task_switching disabled");
		// return 0;
	}

	/* this should really be a no-op, since, interrupts should already be disabled from the ISR. */
	disable_interrupts();
	task_switching = false;

	current_task = new_task;

	/* TODO: Switch page directories here */

	/* Update the TSS */
	assert(current_task->stack != NULL);
	assert(current_task->esp != 0);
	assert(current_task->ss == 0x10);
	tss_switch((uint32)current_task->stack, current_task->esp, current_task->ss);

	task_switching = true;
	//enable_interrupts(); // let the ISR do this

	/* 
	 * Return the ESP of the new task (which is now set as current_task).
	 * The next line of code to execute is the one in the ISR that updates the ESP register to this value.
	 * After that, the ISR pops off all the registers etc. and continues execution at the EIP found
	 * at this ESP value. Since all those belong to the new task, we will have switched tasks.
	 */
	return current_task->esp;
} 

/* This function is called by the IRQ handler whenever the timer fires (or a software interrupt 0x7e is sent). */
uint32 scheduler_taskSwitch(uint32 esp) {
	if (task_switching == false || (current_task == &kernel_task && ready_queue.count == 1))
		return esp;

	/* Save the current ESP (the one for the task we're switching FROM) */
	current_task->esp = esp;

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
			return switch_task(p);
		}
	}

	/* We didn't find any sleeping tasks to wake right now; let's focus on switching tasks as usual instead */
	node_t *old_task_node = list_find_first((list_t *)&ready_queue, (void *)current_task);
    node_t *new_task_node = old_task_node->next;
	if (new_task_node == NULL)
		new_task_node = ready_queue.head;

	node_t *orig_task_node = new_task_node; /* hack: store the task we're currently on in the list (see below) */

	/* Ignore sleeping tasks */
	task_t *new_task = NULL;
	do {
		new_task = (task_t *)new_task_node->data;
		if (new_task->state != TASK_SLEEPING)
			break;
		
		new_task_node = new_task_node->next;
		if (new_task_node == NULL)
			new_task_node = ready_queue.head;

		if (new_task_node == orig_task_node) {
			/* We've looped through all the tasks, and they're all sleeping... */
			panic("No running tasks found!");
		}
	} while (true);

	/* new_task now points towards the task we want to run */

    if (old_task_node == new_task_node) {
		/* no point in switching, eh? */
        return(esp);
	}
	
	/* Looks like we found a running task to switch to! Let's do so. */

    return switch_task(new_task);
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
	assert(current_task->wakeup_time == 0);
	assert(current_task != &kernel_task);

	/* Mark the task as sleeping */
	current_task->state = TASK_SLEEPING;
	current_task->wakeup_time = start_ticks + ticks_to_wait;

	/* Force a task switch */
	asm volatile("int $0x7e");
}
