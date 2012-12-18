#include <kernel/time.h>
#include <sys/types.h>
#include <kernel/kernutil.h>
#include <string.h>

	/*
	 * This is a big chunk of code used for debugging the RTC routines.
	 * It changes status register B of the CMOS to change the use
	 * of 12h/24h time and BCD/binary representations.
	 * It's still here just in case; rewriting it would be boring.
	 */
/*
	// Set status reg B for debugging purposes
	uint8 rb = 0;

	asm volatile(
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
	asm volatile("cli;"
		"movb $0xb, %%al;"
		"outb %%al, $0x70;"
		"movb %[rb], %%al;"
		"nop;nop;nop;nop;nop;nop;nop;nop;"
		"outb %%al, $0x71;"
		"sti;"
		: : [rb]"m"(rb) : "%al", "memory");

	// Set the clock (to debug AM/PM issues)
	asm volatile("cli;"
		"movb $0x4, %%al;" // set the hour...
		"outb %%al, $0x70;"
		"movb $0, %%al;"  // ...to the time specified
		"nop;nop;nop;nop;nop;nop;nop;nop;"
		"outb %%al, $0x71;"
		"sti;"
		: : : "%al", "memory");
*/
	// END DEBUGGING CODE

	uint8 yeartmp = 0;

static bool rtc_update_in_progress(void) {
	outb(0x70, 10);
	return ((inb(0x71) & 0x80) != 0);
}

static uint8 rtc_get_reg(uint8 reg) {
	assert(reg <= 0xb);
	outb(0x70, reg);
	return inb(0x71);
}

#define REG_YEAR 0x09
#define REG_MONTH 0x08
#define REG_DAYOFMONTH 0x07
#define REG_HOUR 0x04
#define REG_MINUTE 0x02
#define REG_SECOND 0x00
#define REG_STATUS_B 0xb

static void rtc_get_raw(Time *t) {
	assert(t != NULL);

	while (rtc_update_in_progress()) {
	}

	t->year   = rtc_get_reg(REG_YEAR);
	t->month  = rtc_get_reg(REG_MONTH);
	t->day    = rtc_get_reg(REG_DAYOFMONTH);
	t->hour   = rtc_get_reg(REG_HOUR);
	t->minute = rtc_get_reg(REG_MINUTE);
	t->second = rtc_get_reg(REG_SECOND);
}

static void rtc_reformat(Time *t) {
	assert(t != NULL);

	/* Fetch CMOS status register B */
	uint8 regb = rtc_get_reg(REG_STATUS_B);

	/*
	 * These bits describe the output from the RTC - whether the data is
	 * in BCD or binary form, and whether the clock is 12-hour or 24-hour.
	 */
	uint8 is_bcd, is_24_hour;
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
#define bcd_to_bin(x) (((x) / 16) * 10) + (uint8)((x) & (uint8)0xf)
		t->year   = bcd_to_bin(t->year);
		t->year  += (t->year < 12 ? 2100 : 2000);
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
}

void get_time(Time *t) {
	//
	// Logic: wait while an update is in progress; when done,
	//
	assert(t != NULL);
	Time prev;
	memset(t, 0, sizeof(Time));

	do {
		memcpy(&prev, t, sizeof(Time));
		while (rtc_update_in_progress()) { }
		rtc_get_raw(t);
	} while(memcmp(&prev, t, sizeof(Time)) != 0);

	// OK, we should have a non-corrupt representation of the current time now,
	// even if the RTC happened to update mid-reading. Reformat it.
	rtc_reformat(t);

	// ... that's it!
}

int gettimeofday(struct timeval *restrict tp, void *restrict tzp) {
	Time t;
	get_time(&t);
	assert(t.year == 2012);

	return 0;
}
