#include <types.h>
#include <string.h>
#include <kernel/kernutil.h>
#include <kernel/kheap.h>
#include <kernel/paging.h>
#include <kernel/monitor.h>

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
