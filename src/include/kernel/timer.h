#ifndef _TIMER_H
#define _TIMER_H

#include <sys/types.h>

/* This is enough to not wrap for 497 days at 100 Hz. */
#define TIMER_DIVISOR ((uint16)11932)
#define TIMER_HZ 100
#define TIMER_MS 10 // reciprocal of 100 Hz
/*const uint16 TIMER_DIVISOR = 11932;*/
/*const uint16 TIMER_HZ = 100;*/
/*const uint16 TIMER_MS = 10; // reciprocal of 100 Hz*/

uint32 uptime(void);
uint32 gettickcount(void);
void delay(uint32 ms);

#endif
