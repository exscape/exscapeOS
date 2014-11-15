#include <kernel/fpu.h>
#include <kernel/console.h>
#include <kernel/kernutil.h>

static uint32 no_fpu_interrupt_handler(uint32 esp);

void fpu_init(void) {
	register_interrupt_handler(EXCEPTION_NO_COPROCESSOR, no_fpu_interrupt_handler);
	asm volatile("fninit");

	// Enable FPU native exceptions (NE) and set the Task Switched (TS) bits.
	// TS is used to only save FPU state after it has been used in a given process.
	asm volatile("mov %%cr0, %%eax;"
				 "or $0x00000010, %%eax;" /* NE = 1 */
//				 "or $0x00000008, %%eax;" /* TS = 1 */
				 "mov %%eax, %%cr0;"
				 : /* no outputs */
				 : /* no inputs */
				 : "%eax", "cc");

}

static uint32 no_fpu_interrupt_handler(uint32 esp) {
	return esp;
}
