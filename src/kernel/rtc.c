#include <kernel/rtc.h>
#include <types.h>

//#define DIVZERO_10_SEC /* divides by zero every 10 seconds; used to test exceptions */

void get_time(Time *t) {
	/* This function isn't pretty, at all, but it didn't appear very easy to share
	   structs between nasm and C, so I chose to use inline assembly. */

	unsigned char yeartmp = 0;

	/*
	 * This is a big chunk of code used for debugging the RTC routines.
	 * It changes status register B of the CMOS to change the use
	 * of 12h/24h time and BCD/binary representations.
	 * It's still here just in case; rewriting it would be boring.
	 */

/*
	// Set status reg B for debugging purposes
	unsigned char rb = 0;

	asm(
	// wait until no update is in progress
		".lltmp:"
		"movb $10, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"testb $0x80, %%al;"
		"jne .lltmp;"

	// fetch status reg B
		"movb $0xb, %%al;" // status reg B
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[rb];"
		: [rb]"=m"(rb) : : "%al", "memory");

	// Force 12h time
	rb &= ~2;

	// set status reg B
	asm("cli;"
		"movb $0xb, %%al;"
		"outb %%al, $0x70;"
		"movb %[rb], %%al;"
		"nop;nop;nop;nop;nop;nop;nop;nop;"
		"outb %%al, $0x71;"
		"sti;"
		: : [rb]"m"(rb) : "%al", "memory");

	// Set the clock (to debug AM/PM issues)
	asm("cli;"
		"movb $0x4, %%al;" // set the hour...
		"outb %%al, $0x70;"
		"movb $0, %%al;"  // ...to the time specified
		"nop;nop;nop;nop;nop;nop;nop;nop;"
		"outb %%al, $0x71;"
		"sti;"
		: : : "%al", "memory");
*/
	// END DEBUGGING CODE

	asm(
		/* make sure the update flag isn't set */
		".ll:"
		"movb $10, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"testb $0x80, %%al;"
		"jne .ll;"

		/* fetch the year (00-99) */
		"movb $0x09, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[yeartmp];"

		/* month */
		"movb $0x08, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[month];"

		/* day of month */
		"movb $0x07, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[day];"

		/* hour (12h or 24h) */
		"movb $0x04, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[hour];"

		/* minute */
		"movb $0x02, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[minute];"

		/* second */
		"movb $0x0, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[second];"

		:
		[yeartmp]"=m"(yeartmp),
		[month]"=m"(t->month),
		[day]  "=m"(t->day),
		[hour] "=m"(t->hour),
		[minute]"=m"(t->minute),
		[second]"=m"(t->second)
		: : "%al", "memory");

	/* Fetch CMOS status register B */
	unsigned char regb;
	asm("movb $0xb, %%al;" // 0xb = status reg B
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[regb];"
		:
		[regb]"=m"(regb)
		: : "%al", "memory");

	/* These bits describe the output from the RTC - whether the data is
	 * in BCD or binary form, and whether the clock is 12-hour or 24-hour.
	 */
	unsigned char is_bcd, is_24_hour;
	is_bcd     = (regb & 4) ? 0 : 1; /* [sic] */
	is_24_hour = (regb & 2) ? 1 : 0; 

	uint8 time_is_pm = 0;

	/*
	 * If this is a 12-hour clock, mask out the 0x80 bit if it's set.
	 * If it is, the time is PM. If not, the time is AM.
	 * This is used below, after BCD decoding.
	 */
	if (!is_24_hour) {
		if ((t->hour & 0x80) != 0) {
			// Remove the bit (to make the hour readable)
			t->hour &= ~0x80;
			// Save this knowledge, though; it's used below
			time_is_pm = 1;
		}
	}

	/* Convert the data from BCD to binary form, if needed */
	if (is_bcd) {
#define bcd_to_bin(x) ((x / 16) * 10) + (unsigned char)(x & (unsigned char)0xf)
		yeartmp   = bcd_to_bin(yeartmp);
		t->month  = bcd_to_bin(t->month);
		t->day    = bcd_to_bin(t->day);
		t->hour   = bcd_to_bin(t->hour);
		t->minute = bcd_to_bin(t->minute);
		t->second = bcd_to_bin(t->second);
	}

	if (!is_24_hour) {
		/* Midnight is stored as 12PM; we want to display it as 00 */
		if (time_is_pm && t->hour == 12)
			t->hour = 0;
		else if (time_is_pm) {
			/* Otherwise, just move it to 24-hour times, as usual */
			t->hour += 12;
		}
	}

/* Used to debug exception handling */
#ifdef DIVZERO_10_SEC
	if (t->second % 10 == 0) {
		asm("mov $0xDEADBEEF, %eax;"
			"mov $0, %ebx;"
			"div %ebx;"); 
	}
#endif

	/*
	 * This will fail for year 2100+, and I frankly don't care enough to fix it.
	 * Actually, I would fix this if I was *sure* it was always stored it register 0x32, but that appears uncertain...?
	 */
	t->year = 2000 + yeartmp; 
}
