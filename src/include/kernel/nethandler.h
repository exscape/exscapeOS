#ifndef _NETHANDLER_H
#define _NETHANDLER_H

#include <string.h>
#include <kernel/timer.h>
#include <kernel/task.h>

#define NETHANDLER_NAME_SIZE 16

typedef struct {
	task_t *task;
	list_t *tasks;
	char name[NETHANDLER_NAME_SIZE];
} nethandler_t;

nethandler_t *nethandler_create(const char *name);
void nethandler_add(nethandler_t *worker, void (*func)(void *, uint32), void *data, uint32 length, uint8 priority);
void nethandler_task(void *data, uint32 length);

#endif
