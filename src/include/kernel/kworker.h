#ifndef _KWORKER_H
#define _KWORKER_H

#include <string.h>
#include <kernel/timer.h>
#include <kernel/task.h>

#define KWORKER_NAME_SIZE 16

typedef struct {
	task_t *task;
	list_t *tasks;
	char name[KWORKER_NAME_SIZE];
} kworker_t;

kworker_t *kworker_create(const char *name);
void kworker_add(kworker_t *worker, void (*func)(void *, uint32), void *data, uint32 length, uint8 priority);
void kworker_task(void *data, uint32 length);

#endif
