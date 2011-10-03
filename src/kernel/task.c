#include <types.h>
#include <string.h>
#include <kernel/kernutil.h>
#include <kernel/kheap.h>
#include <kernel/paging.h>
#include <kernel/monitor.h>
#include <kernel/task.h>
#include <kernel/gdt.h> /* set_kernel_stack */

/* Externs; three from paging.c, one from an assembly file */
extern page_directory_t *kernel_directory;
extern page_directory_t *current_directory;
extern void alloc_frame_to_page(page_t *, bool, bool);
extern uint32 read_eip(void);

volatile bool task_switching = false;

uint32 next_pid = 2; /* kernel_task has PID 1 */

task_t kernel_task = {
	.id = 1,
	.esp = 0,
	.ebp = 0,
	.ss = 0,
	.eip = 0,
	.stack = 0, /* not used for this task */
	.page_directory = 0,
	.next = 0
};

/* Our globals */
volatile task_t *current_task = &kernel_task; // the currently running task
volatile task_t *ready_queue = &kernel_task;  // the start of the task linked list

void init_tasking(void) {
	disable_interrupts();

	kernel_task.page_directory = kernel_directory;

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
	task->stack = (void *)( (uint32)kmalloc_a(8192) + 8192 );
	task->page_directory = current_directory;
	task->next = 0;

	/* Set up the kernel stack of the new process */
	uint32 *kernelStack = task->stack;
	uint32 code_segment = 0x08;

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
	enable_interrupts();

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
