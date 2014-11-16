#include <kernel/fpu.h>
#include <kernel/console.h>
#include <kernel/kernutil.h>
#include <kernel/task.h>

// x87_MMX_SSE_SSE2_SSE3_StateOwner from the Intel docs
task_t *last_fpu_task = NULL;

static uint32 no_fpu_interrupt_handler(uint32 esp);

void fpu_init(void) {
	register_interrupt_handler(EXCEPTION_NO_COPROCESSOR, no_fpu_interrupt_handler);
	asm volatile("fninit");

	// Enable FPU native exceptions (NE) and set the Task Switched (TS) bits.
	// TS is used to only save FPU state after it has been used in a given process.
	asm volatile("mov %%cr0, %%eax;"
				 "or $0x00000010, %%eax;" /* NE = 1 */
				 "or $0x00000008, %%eax;" /* TS = 1 */
				 "mov %%eax, %%cr0;"
				 : /* no outputs */
				 : /* no inputs */
				 : "%eax", "cc");

	// Set the CR4 OSFXSR flag, which enables FXSAVE/FXRSTOR to save SSE* state.
	asm volatile("mov %%cr4, %%eax;"
				 "or $0x00000200, %%eax;" /* OSFXSR = 1 */
				 "mov %%eax, %%cr4;"
				 : /* no outputs */
				 : /* no inputs */
				 : "%eax", "cc");
}

static uint32 no_fpu_interrupt_handler(uint32 esp) {
	// First of all, we must clear TS, or we we'll get stuck in a loop:
	// FPU instuction -> #NM -> this code -> FPU instruction -> ...
	asm volatile("clts");

	// Next, save the state for the previous FPU task, if any
	if (last_fpu_task != NULL) {
		assert(last_fpu_task->fpu_state != NULL);
		asm volatile("fxsave %0" : "=m"(*last_fpu_task->fpu_state) : : "memory");
	}

	if (!current_task->has_used_fpu) {
		// Initialize the FPU registers and set the state to something sane.
		asm volatile("fninit");
		asm volatile("fxsave %0"
					 :"=m"(*current_task->fpu_state) : : "memory");

		current_task->has_used_fpu = true;
	}
	else {
		// Restore the state for the current task, as it's currently trying to use it
		asm volatile("fxrstor %0" : : "m"(*current_task->fpu_state));
	}

	last_fpu_task = (task_t *)current_task;

	return esp;
}
