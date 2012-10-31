#ifndef _KWORKER_H
#define _KWORKER_H

#include <string.h>
#include <kernel/timer.h>
#include <kernel/task.h>

void kworker_init(void);
void kworker_add(void (*func)(void *, uint32), void *data, uint32 length, uint8 priority);
void kworker_task(void);

#endif
