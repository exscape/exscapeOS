#include <kernutil.h>
#include <gdtidt.h> /* register_interrupt_handler */
#include <monitor.h>

/* This is enough to not wrap for 497 days at 100 Hz. */
uint32 timer_ticks = 0;
const uint16 TIMER_DIVISOR = 11932;
const uint16 TIMER_HZ = 100;

uint32 gettickcount(void) {
	/* Returns the number of ticks that have passed since reboot. */
	return timer_ticks;
}

uint32 uptime(void) {
	/* Returns the system uptime, in seconds. */
	return timer_ticks / TIMER_HZ;
}

void sleep(uint32 milliseconds) {
	const uint32 start_ticks = timer_ticks;

	if (milliseconds == 0)
		return;

	while (timer_ticks < start_ticks + milliseconds/10);
}

void timer_handler(registers_t regs) {
	/* Increase the tick count */
	timer_ticks++;
/*
	if (timer_ticks % 100 == 0) {
		// 1 second has passed, pretty much
		printk("1 second. Timer: %u\n", timer_ticks);
	}
*/
}

/*
 * TODO: calculate the timer drift properly.
 * I *think* the error comes to ~1.3 seconds per 24 hours, but
 * I don't feel too certain about the calculations...
 */

void timer_install(void) {
	/* 
     * Set the timer frequency
     * If the oscillator works at 1 193 182 Hz, dividing by 11932 gives
     * 99.9985 Hz, the closest possible to 100 (with an integer divisor, of course).
     */
	outb(0x43, 0x36);
	outb(0x40, TIMER_DIVISOR & 0xff); /* set low byte */
	outb(0x40, (TIMER_DIVISOR >> 8) & 0xff); /* set high byte */

	/* Install the timer handler */
	register_interrupt_handler(IRQ0, timer_handler);
}
