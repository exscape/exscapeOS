#include <string.h>
#include <kernel/timer.h>
#include <kernel/task.h>
#include <kernel/kernutil.h>
#include <kernel/kheap.h>
#include <kernel/nethandler.h>

/*
 * This file implements a task that does miscellaneous kernel-mode tasks,
 * such as handle received network packets, which would otherwise be
 * handled in code called from ISRs, where interrupts would be disabled
 * and the OS "frozen". Since having the OS "stopped" like that for longer
 * periods of time is unacceptable, the ISR instead tells the kernel worker
 * to take care of it.
 */

nethandler_t *nethandler_arp = NULL;
nethandler_t *nethandler_icmp = NULL;

typedef struct {
	void (*function)(void *, uint32);
	void *data;
	uint32 length; // data size in bytes
	uint8 priority;
} nethandler_task_t;

nethandler_t *nethandler_create(const char *name) {
	assert(strlen(name) + 1 <= NETHANDLER_NAME_SIZE);
	nethandler_t *worker = kmalloc(sizeof(nethandler_t));
	strlcpy(worker->name, name, NETHANDLER_NAME_SIZE);
	worker->tasks = list_create();
	worker->task = create_task(nethandler_task, name, &kernel_console, worker, sizeof(nethandler_t));

	return worker;
}

void nethandler_add(nethandler_t *worker, void (*func)(void *, uint32), void *data, uint32 length, uint8 priority) {
	assert(worker != NULL);
	assert(data != NULL);
	assert(length > 0);

	nethandler_task_t *task = kmalloc(sizeof(nethandler_task_t));
	task->function = func;

	if (length > 0) {
		task->data = kmalloc(length);
		memcpy(task->data, data, length);
	}
	else
		task->data = NULL;

	task->length = length;
	task->priority = priority;

	list_append(worker->tasks, task);
}

static void nethandler_remove(nethandler_t *worker, node_t *node) {
	assert(worker != NULL);
	assert(worker->tasks != NULL);
	assert(node != NULL);

	// It would be less ugly to pass the task as a parameter instead,
	// but that would mean we would have to use list_find_first() to
	// find the node again, and pass that to list_remove().
	// Efficiency wins over beauty here.
	nethandler_task_t *task = node->data;

	if (task->data)
		kfree(task->data);
	list_remove(worker->tasks, node);
	kfree(task);
}

// The *process* that does all the work. I'll try to come up with better naming
// than to have "task" mean two different things...
void nethandler_task(void *data, uint32 length) {
	nethandler_t *worker = (nethandler_t *)data;

	while (true) {
		while (list_size(worker->tasks) == 0) {
			// Nothing to do; switch to some other task, that can actually do something
			asm volatile("int $0x7e");
		}

		if (list_size(worker->tasks) == 1) {
			// No point in checking priority if there's only one task! Take care of this one.
			node_t *node = worker->tasks->head;
			nethandler_task_t *task = (nethandler_task_t *)node->data;

			assert(task->function != NULL);
			task->function(task->data, task->length);
			nethandler_remove(worker, node);
		}
		else {
			// Run the task with the highest priority first. Priority is a simple integer
			// level, so 255 is the maximum, while 0 is the minimum.
			node_t *max = worker->tasks->head;
			for (node_t *it = worker->tasks->head; it != NULL; it = it->next) {
				nethandler_task_t *task = (nethandler_task_t *)it->data;
				if (task->priority > ((nethandler_task_t *)it->data)->priority) {
					max = it;
				}
			}
			nethandler_task_t *max_task = (nethandler_task_t *)max->data;
			assert(max_task != NULL);
			assert(max_task->function != NULL);

			max_task->function(max_task->data, max_task->length);
			nethandler_remove(worker, max);
		}
	}
}
