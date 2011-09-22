#include <types.h>
#include <string.h>
#include <kernel/kernutil.h>
#include <kernel/kheap.h>
#include <kernel/paging.h>
#include <kernel/monitor.h>

extern page_directory_t *kernel_directory;
extern page_directory_t *current_directory;
extern uint32 initial_esp;

void move_stack(void *new_stack_start, uint32 size) {

/* TODO FIXME: if this isn't called DIRECTLY from kmain (i.e. is called from init_tasking() or such), DEBUG THE EBP-UPDATING CODE!
 * It's only tested from main, i.e. with a single stack frame in between.
 */

	uint32 addr;

	for (addr = (uint32)new_stack_start; addr >= ((uint32)new_stack_start) - size; addr -= 0x1000) {
	/* This stack is in user mode */
	alloc_frame(addr, current_directory, 0 /* user */, 1 /* writable */); /* TODO: #defines */
	/* alloc_frame calls INVLPG for us */
	}

	/* TODO: remove this; everything should still work (see above!). Just to be sure, before everything in finished... */
	flush_all_tlb();

	/* Read the stack pointers */
	uint32 old_esp;
	uint32 old_ebp;
	asm volatile ("mov %%esp, %0" : "=r"(old_esp));
	asm volatile ("mov %%ebp, %0" : "=r"(old_ebp));

	/* The "difference in location" between the old and new stack; we need to figure out where to point ESP and EBP from now on */
	uint32 offset = (uint32)new_stack_start - initial_esp;
	uint32 new_esp = old_esp + offset;
	uint32 new_ebp = old_ebp + offset;

	/* Copy the stack, data and all */
	memcpy((void *)new_esp, (void *)old_esp, initial_esp - old_esp);

	/* 
	 * Okay... This was inspired by a post by JamesM on the osdev forums. By inspired, I mean that he gave the pseudocode
	 * and I implemented + tested it. It appears to work just fine so far!
	 * The code updates all the pushed EBP values (pushed in the beginning of each function call), so that they point
	 * to the NEW stack location, rather than the old. If we don't do this, function returns will pop off EBP values that point
	 * to the old stack. Not good.
	 * The way this works is that each EBP value on a stack is the address of the NEXT (or previous, however you see it)
	 * pushed EBP value. So *EBP (for the current EBP value) is the previous stored EBP value. *(*EBP) is the 2nd previous, etc.
	 */

	/* We can't modify /new_ebp/, since we use it below (to set as the current base pointer!) */
	uint32 ebp_ptr = new_ebp;
	while (true) {
		/* Extract the value stored wherever the last EBP pointed */
		uint32 found = *( (uint32 *)ebp_ptr );

		/* Is the value we found an EBP pointer that is pointing towards the old stack? */
		if (found >= old_esp && found <= initial_esp) {
			/* Yes, it is. Change it to point to the new stack, by adding the offset to the value we found */
			*( (uint32 *)ebp_ptr ) = found + offset;

			/* Prepare for the next loop */
			ebp_ptr = found;
		}
		else {
			/* This value doesn't point to the old stack; thus, we are done. (It probably points to the bootloader's (or GRUB's) stack,
			 * somewhere around 0x67000 */
			break;
		}
	}

	/* Update the stack pointers to their new values */
	asm volatile ("mov %0, %%esp" : : "r"(new_esp));
	asm volatile ("mov %0, %%ebp" : : "r"(new_ebp));

	/* Overwrite the old stack, just to make sure that it's not silently used! This ought to make sure we notice... */
	memset((void *)old_esp, 0xbb, initial_esp - old_esp);

/*
 * THIS CODE BREAKS THE STACK!!!
 * It is ONLY kept for reference until I'm 100% sure the above works.
 * There is, however, little doubt that it's better than this mess!
 */
/*
	for (addr = (uint32)new_stack_start; addr > (uint32)new_stack_start - size; addr -= 4) {
		uint32 tmp = *( (uint32 *) addr );

		if (old_esp < tmp && tmp < initial_esp) {
			// Make sure the value is also in some reasonable range (the old stack)
			if ( *((uint32 *)tmp) <= initial_esp && *((uint32 *)tmp) >= initial_esp + initial_stacksize) {
				tmp += offset;
				uint32 *tmp2 = (uint32 *)addr;
				*tmp2 = tmp;
			}
		}
	}
*/
}
