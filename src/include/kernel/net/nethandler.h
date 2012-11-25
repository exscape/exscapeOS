#ifndef _NETHANDLER_H
#define _NETHANDLER_H

#include <string.h>
#include <kernel/timer.h>
#include <kernel/task.h>
#include <kernel/mutex.h>

#define NETHANDLER_NAME_SIZE 20
#define NETHANDLER_NUM_BUFFERS 8
#define NETHANDLER_BUFFER_SIZE 1536 /* Exactly 1500 ought to be enough */

typedef enum {
	EMPTY,
	NEEDS_PROCESSING,
	CURRENTLY_PROCESSING
	/* PROCESSED would be the same as EMPTY */
} nethandler_buffer_state;

typedef struct {
	uint8 buffer[NETHANDLER_BUFFER_SIZE];
	uint32 length;
	nethandler_buffer_state state;
} nethandler_buffer_t;

typedef struct {
	task_t *task;
	char name[NETHANDLER_NAME_SIZE];
	nethandler_buffer_t *buffers[NETHANDLER_NUM_BUFFERS];
	void (*function)(void *, uint32);
} nethandler_t;

nethandler_t *nethandler_create(const char *name, void (*func)(void *, uint32));
void nethandler_add_packet(nethandler_t *worker, void *data, uint32 length);
void nethandler_task(void *data, uint32 length);

#endif
