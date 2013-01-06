#ifndef _KERNEL_TIME_H
#define _KERNEL_TIME_H

#include <sys/types.h>
#include <sys/time.h>

void get_time(Time *);
time_t kern_mktime(Time *); // Note that the same rules as mktime() uses apply, e.g. month is 0 to 11, year is "years since 1900" etc.
time_t kern_time(void);

// mktime stuff, originally from Newlib

#define SECSPERMIN	60L
#define MINSPERHOUR	60L
#define HOURSPERDAY	24L
#define SECSPERHOUR	(SECSPERMIN * MINSPERHOUR)
#define SECSPERDAY	(SECSPERHOUR * HOURSPERDAY)
#define DAYSPERWEEK	7
#define MONSPERYEAR	12

#define YEAR_BASE	1900
#define EPOCH_YEAR      1970
#define EPOCH_WDAY      4
#define EPOCH_YEARS_SINCE_LEAP 2
#define EPOCH_YEARS_SINCE_CENTURY 70
#define EPOCH_YEARS_SINCE_LEAP_CENTURY 370

#define isleap(y) ((((y) % 4) == 0 && ((y) % 100) != 0) || ((y) % 400) == 0)

#endif
