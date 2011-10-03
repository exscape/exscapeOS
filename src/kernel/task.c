#include <types.h>
#include <string.h>
#include <kernel/kernutil.h>
#include <kernel/kheap.h>
#include <kernel/paging.h>
#include <kernel/monitor.h>
#include <kernel/task.h>
#include <kernel/gdt.h> /* set_kernel_stack */

/* Externs from paging.c */
extern page_directory_t *kernel_directory;
extern page_directory_t *current_directory;
extern void alloc_frame_to_page(page_t *, bool, bool);

volatile bool task_switching = false;

#define KERNEL_STACK_SIZE 8192

uint32 next_pid = 2; /* kernel_task has PID 1 */

task_t kernel_task = {
	.id = 1,
	.esp = 0,
	.ebp = 0,
	.ss = 0x10,
	.eip = 0,
	.stack = 0, /* set later */
	.page_directory = 0,
	.next = 0
};

/* Our globals */
volatile task_t *current_task = &kernel_task; // the currently running task
volatile task_t *ready_queue = &kernel_task;  // the start of the task linked list

void kill(task_t *task) {
	assert(task != (task_t *)&kernel_task);
	task_switching = false;

	/* TODO: destroy user page directory */

	/* Delete this task from the queue */
	/* Since the list is singly-linked, we need to hack acound that a bit. */

	task_t *p = (task_t *)ready_queue;
	if (p == task) {
		ready_queue = task->next;
	}
	else {
		while (p != NULL && p->next != NULL) {
			if (p->next == task) {
				p->next = task->next;
			}
			p = p->next;
		}
	}

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
	task_t *task = (task_t *)ready_queue;
	while (task->id != pid && task->next != NULL)
		task = task->next;

	if (task->id == pid) {
		kill(task);
		return true;
	}
	else {
		/* we didn't find the task! */
		return false;
	}

}

void exit_proc(void) {
	kill((task_t *)current_task);
}

void init_tasking(uint32 kerntask_esp0) {
	disable_interrupts();

	kernel_task.page_directory = kernel_directory;
	kernel_task.stack = (void *)kerntask_esp0;

	task_switching = true;
	enable_interrupts();
}

task_t *create_task( void (*entry_point)(void) ) {
	disable_interrupts(); /* not sure if this is needed */
	task_switching = false;

	task_t *task = kmalloc(sizeof(task_t));
	memset(task, 0, sizeof(task_t));

	task->id = next_pid++;
	task->ebp = 0; // FIXME: is this OK?
	task->esp = 0;
	task->eip = 0;
	task->stack = (void *)( (uint32)kmalloc_a(KERNEL_STACK_SIZE) + KERNEL_STACK_SIZE );
	task->page_directory = current_directory;
	task->next = 0;

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

	/* Data segments (DS, ES, FS, GS I assume, need to look at the ISR. TODO */
	*(--kernelStack) = data_segment;
	*(--kernelStack) = data_segment;
	*(--kernelStack) = data_segment;
	*(--kernelStack) = data_segment;

	task->esp = (uint32)kernelStack;
	task->ss = data_segment;

	/* Add the new task to the end of the task queue */
	task_t *tmp = (task_t *)ready_queue;
	while (tmp->next)
		tmp = tmp->next;
	tmp->next = task;

	task_switching = true;
	enable_interrupts(); /* not sure if this is needed */
	return task;
}

uint32 switch_task(task_t *new_task) {
	if (current_task == NULL || task_switching == false) {
		panic("switch_task with no current_task or task_switching disabled");
		// return 0;
	}

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

	return current_task->esp;
} 

uint32 scheduler_taskSwitch(uint32 esp) {
	if (task_switching == false || (current_task == &kernel_task && current_task->next == NULL))
		return esp;

	//task_saveState(esp);
	current_task->esp = esp; // same as the commented out version above

	task_t* old_task = (task_t*)current_task;
    task_t* new_task = current_task->next;
	if (new_task == NULL)
		new_task = (task_t *)ready_queue;

    if (old_task == new_task) {
		/* no point in switching, eh? */
        return(esp);
	}

    return switch_task(new_task);
}

int getpid(void) {
	return current_task->id;
}
