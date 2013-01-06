#include <kernel/time.h>
#include <sys/types.h>
#include <kernel/kernutil.h>
#include <string.h>
#include <sys/errno.h>
#include <kernel/vmm.h>

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
	// Logic: wait while an update is in progress; when done, read the date/time
	// until the last two readings are equal. This way, even if the readings
	// change mid-read, we will still get a consistent reading in the end.
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

/*****************************************************************
 * ABOVE: RTC handling code.                                     *
 * BELOW: kern_mktime() and gettimeofday() plus helper functions *
 *****************************************************************/

typedef struct {
	int quot;
	int rem;
} div_t;

static div_t div(int a, int b) {
	div_t d = {0};
	if (b == 0)
		return d;

	d.quot = a / b;
	d.rem = a % b;

	return d;
}

#define _SEC_IN_MINUTE 60L
#define _SEC_IN_HOUR 3600L
#define _SEC_IN_DAY 86400L

static const int DAYS_IN_MONTH[12] =
{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

#define _DAYS_IN_MONTH(x) ((x == 1) ? days_in_feb : DAYS_IN_MONTH[x])

static const int _DAYS_BEFORE_MONTH[12] =
{0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

#define _ISLEAP(y) (((y) % 4) == 0 && (((y) % 100) != 0 || (((y)+1900) % 400) == 0))
#define _DAYS_IN_YEAR(year) (_ISLEAP(year) ? 366 : 365)

static void validate_structure (Time *tim_p) {
  div_t res;
  int days_in_feb = 28;

  /* calculate time & date to account for out of range values */
  if (tim_p->second > 59)
    {
      res = div (tim_p->second, 60);
      tim_p->minute += res.quot;
    }

  if (tim_p->minute > 59)
    {
      res = div (tim_p->minute, 60);
      tim_p->hour += res.quot;
    }

  if (tim_p->hour > 23)
    {
      res = div (tim_p->hour, 24);
      tim_p->day += res.quot;
    }

  if (tim_p->month > 11)
    {
      res = div (tim_p->month, 12);
      tim_p->year += res.quot;
    }

  if (_DAYS_IN_YEAR (tim_p->year) == 366)
    days_in_feb = 29;

	while (tim_p->day > _DAYS_IN_MONTH (tim_p->month))
	{
	  tim_p->day -= _DAYS_IN_MONTH (tim_p->month);
	  if (++tim_p->month == 12)
	    {
	      tim_p->year++;
	      tim_p->month = 0;
	      days_in_feb =
		((_DAYS_IN_YEAR (tim_p->year) == 366) ?
		 29 : 28);
	    }
	}
}

time_t kern_mktime(Time *tim_p) {
  time_t tim = 0;
  long days = 0;
  int year;

  /* validate structure */
  validate_structure (tim_p);

  /* compute hours, minutes, seconds */
  tim += tim_p->second + (tim_p->minute * _SEC_IN_MINUTE) +
    (tim_p->hour * _SEC_IN_HOUR);

  /* compute days in year */
  days += tim_p->day - 1;
  days += _DAYS_BEFORE_MONTH[tim_p->month];
  if (tim_p->month > 1 && _DAYS_IN_YEAR (tim_p->year) == 366)
    days++;

  /* compute days in other years */
  if ((year = tim_p->year) > 70)
    {
      for (year = 70; year < tim_p->year; year++)
	days += _DAYS_IN_YEAR (year);
    }
  else if (year < 70)
    {
      for (year = 69; year > tim_p->year; year--)
	days -= _DAYS_IN_YEAR (year);
      days -= _DAYS_IN_YEAR (year);
    }

  /* compute total seconds */
  tim += (days * _SEC_IN_DAY);

  return tim;
}

int gettimeofday(struct timeval *restrict tp, void *restrict tzp __attribute__((unused))) {
	if (tp == NULL)
		return -EFAULT;

	Time kt;
	get_time(&kt);
	assert(kt.month > 0);
	kt.month--; // convert from [1,12] to [0,11] as mktime expects
	kt.year -= 1900; // convert from year to "year since 1900"
	kt.hour++; // TODO: timezone hack; the code above corrects if this overflows

	time_t t = kern_mktime(&kt);

	tp->tv_sec = t;
	tp->tv_usec = 0;

	return 0;
}

int sys_gettimeofday(struct timeval *restrict tp, void *restrict tzp __attribute__((unused))) {
	if (!CHECK_ACCESS_WRITE(tp, sizeof(struct timeval)))
		return -EFAULT;
	return gettimeofday(tp, tzp); // TODO: check tzp
}

time_t kern_time(void) {
	// Get the current UNIX timestamp
	struct timeval t;
	gettimeofday(&t, NULL);

	return t.tv_sec;
}
