#include <string.h>
#include <kernel/timer.h>
#include <kernel/task.h>
#include <kernel/kernutil.h>
#include <kernel/kheap.h>
#include <kernel/kworker.h>

#include <kernel/arp.h> // XXX TODO FIXME: remove this when arp debugging is done

/*
 * This file implements a task that does miscellaneous kernel-mode tasks,
 * such as handle received network packets, which would otherwise be
 * handled in code called from ISRs, where interrupts would be disabled
 * and the OS "frozen". Since having the OS "stopped" like that for longer
 * periods of time is unacceptable, the ISR instead tells the kernel worker
 * to take care of it.
 */

list_t *kworker_tasks = NULL; /* Not task as in process; task as in "thing to do" */

typedef struct {
	void (*function)(void *, uint32);
	void *data;
	uint32 length; // data size in bytes
	uint8 priority;
} kworker_task_t;

void kworker_init(void) {
	kworker_tasks = list_create();
}

void kworker_add(void (*func)(void *, uint32), void *data, uint32 length, uint8 priority) {
	assert(kworker_tasks != NULL);
	assert(data != NULL);
	assert(length > 0);

	kworker_task_t *task = kmalloc(sizeof(kworker_task_t));
	task->function = func;

	if (length > 0) {
		task->data = kmalloc(length);
		memcpy(task->data, data, length);
	}
	else
		task->data = NULL;

	task->length = length;
	task->priority = priority;

	list_append(kworker_tasks, task);
	if (task->function == arp_handle_request)
		printk("kworker_add: added arp_handle_request\n");
}

// The *process* that does all the work. I'll try to come up with better naming
// than to have "task" mean two different things...
void kworker_task(void *data, uint32 length) {
	while (true) {
		while (list_size(kworker_tasks) == 0) {
			// Nothing to do; switch to some other task, that can actually do something
			asm volatile("int $0x7e");
		}

		if (kworker_tasks->count == 1) {
			// No point in checking priority if there's only one task! Take care of this one.
			node_t *node = kworker_tasks->head;
			kworker_task_t *task = (kworker_task_t *)node->data;

			assert(task->function != NULL);
			if (task->function == arp_handle_request)
				printk("kworker_task: executing arp_handle_request (lone task)\n");
			task->function(task->data, task->length);
			if (task->data)
				kfree(task->data);
			list_remove(kworker_tasks, node);
			kfree(task);
		}
		else {
			// Run the task with the highest priority first. Priority is a simple integer
			// level, so 255 is the maximum, while 0 is the minimum.
			node_t *max = kworker_tasks->head;
			for (node_t *it = kworker_tasks->head; it != NULL; it = it->next) {
				kworker_task_t *task = (kworker_task_t *)it->data;
				if (task->priority > ((kworker_task_t *)it->data)->priority) {
					max = it;
				}
			}
			kworker_task_t *max_task = (kworker_task_t *)max->data;
			assert(max_task != NULL);
			assert(max_task->function != NULL);

			if (max_task->function == arp_handle_request)
				printk("kworker_task: executing arp_handle_request (highest priority)\n");
			max_task->function(max_task->data, max_task->length);
			if (max_task->data)
				kfree(max_task->data);
			list_remove(kworker_tasks, max);
			kfree(max_task);
		}
	}
}
