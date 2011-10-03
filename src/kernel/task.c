#include <types.h>
#include <string.h>
#include <kernel/kernutil.h>
#include <kernel/kheap.h>
#include <kernel/paging.h>
#include <kernel/monitor.h>
#include <kernel/task.h>
#include <kernel/gdt.h> /* set_kernel_stack */

/* Our globals */
volatile task_t *current_task; // the currently running task
volatile task_t *ready_queue;  // the start of the task linked list

/* Externs; three from paging.c, one from an assembly file */
extern page_directory_t *kernel_directory;
extern page_directory_t *current_directory;
extern void alloc_frame_to_page(page_t *, bool, bool);
extern uint32 read_eip(void);

uint32 next_pid = 1;

int schedule(void) {
	/* TODO: this is a temporary function... or is it? */

	current_task = current_task->next;
	if (current_task == NULL)
		current_task = ready_queue;

	current_directory = current_task->page_directory;
	set_kernel_stack(current_task->stack);

	return 1;
}

void init_tasking(void) {
	disable_interrupts();

	/* Initialize the kernel task */
	/* TODO: does it work to simply set esp/ebp/eip to 0 here? */
	current_task = ready_queue = (task_t *)kmalloc(sizeof(task_t));
	current_task->id = next_pid++;
	current_task->esp = current_task->ebp = 0;
	current_task->eip = 0;
	current_task->page_directory = current_directory;
	current_task->next = 0;
	current_task->stack = kmalloc_a(8192); // Hmm. FIXME

	enable_interrupts();
}

task_t *create_task( void (*entry_point)(void) ) {
	disable_interrupts(); /* not sure if this is needed */
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

	enable_interrupts(); /* not sure if this is needed */
	return task;
}

void switch_task(void) {
	/* Since this will be called by the timer, probably before we're done booting, make sure to 
	 * exit gracefully if that occurs. */
	if (current_task == NULL)
		return;

	/* If the current task is the ONLY task, don't attempt to switch */
	if (current_task == ready_queue && ready_queue->next == NULL)
		return;

	uint32 esp, ebp, eip;
	asm volatile("mov %%esp, %0" : "=r"(esp));
	asm volatile("mov %%ebp, %0" : "=r"(ebp));

	// Read the instruction pointer. We do some cunning logic here:
   // One of two things could have happened when this function exits -
   // (a) We called the function and it returned the EIP as requested.
   // (b) We have just switched tasks, and because the saved EIP is essentially
   // the instruction after read_eip(), it will seem as if read_eip has just
   // returned.
   // In the second case we need to return immediately. To detect it we put a dummy
   // value in EAX further down at the end of this function. As C returns values in EAX,
   // it will look like the return value is this dummy value! (0x12345).
   eip = read_eip();

   // Have we just switched tasks?
   if (eip == 0x12345)
       return; 

	/* We didn't switch tasks just now, so let's prepare to do so! */
    /* Reset the CURRENT (pre-switch) task to the if statement above */
	current_task->eip = eip;
	current_task->esp = esp;
	current_task->ebp = ebp;
	/* TODO: the following 4 lines are new */
	if (!schedule()) return;
	eip = current_task->eip;
	esp = current_task->esp;
	ebp = current_task->ebp;

#if 0	
	/* Get the next task to run. If we reached the end of the list, start over at the beginning. */
	current_task = current_task->next;
	if (current_task == NULL)
		current_task = ready_queue;

	/* current_task is now pointed to the NEW task - the one we are going to switch to */

	esp = current_task->esp;
	ebp = current_task->ebp;
#endif

	// Here we:
   // * Stop interrupts so we don't get interrupted.
   // * Temporarily put the new EIP location in ECX.
   // * Load the stack and base pointers from the new task struct.
   // * Change page directory to the physical address (physicalAddr) of the new directory.
   // * Put a dummy value (0x12345) in EAX so that above we can recognise that we've just
   // switched task.
   // * Restart interrupts. The STI instruction has a delay - it doesn't take effect until after
   // the next instruction.
   // * Jump to the location in ECX (remember we put the new EIP in there).
   asm volatile("         \
     cli;                 \
     mov %0, %%ecx;       \
     mov %1, %%esp;       \
     mov %2, %%ebp;       \
     mov %3, %%cr3;       \
     mov $0x12345, %%eax; \
     sti;                 \
     jmp *%%ecx           "
                : : "r"(eip), "r"(esp), "r"(ebp), "r"(current_directory->physical_address));
} 

#if 0
/* Now in assembly! See usermode.s */
void switch_to_user_mode(void) {
	/* Set up the segment registers and the stack for user mode */

	/* iret expects the following on the stack:
	 * 1) an EIP value to jump to
	 * 2) a CS selector
	 * 3) an EFLAGS register to load
	 * 4) a stack pointer to load
	 * 5) a stack segment selector
	 */
	asm volatile("cli;"
				 "mov $0x23, %ax;" /* user data segment (0x20) OR 3 (3 meaning ring 3) */
				 "mov %ax, %ds;"   /* set all the data segments */
				 "mov %ax, %es;"
				 "mov %ax, %fs;"
				 "mov %ax, %gs;"

				 "mov %esp, %eax;" 
				 "pushl $0x23;"   /* SS selector */
				 "pushl %eax;"    /* stack pointer */
				 "pushf;"         /* EFLAGS */
				 "pop %eax;"      /* read EFLAGS back */
				 "or $0x200, %eax;" /* Set the Interrupt Flag (we can't use sti in user mode!) */
				 "push %eax;"     /* write it back */
				 "pushl $0x1b;"   /* CS selector */
				 "push $1f;"      /* EIP (the location of the 1: label below) */
				 "iret;"          /* let's go! */
				 "1:");
}
#endif

int getpid(void) {
	return current_task->id;
}
