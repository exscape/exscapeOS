#include <string.h>
#include <kernel/timer.h>
#include <kernel/task.h>
#include <kernel/kernutil.h>
#include <kernel/heap.h>
#include <kernel/net/nethandler.h>

/*
 * This "class" (collection functions and structs, rather) handles incoming network packets.
 * Previously, this was done in the ISR itself, which is a bad idea for several reasons.
 * After that, I used a similar approach to this one, except that it copied the data to a 
 * malloc'ed buffer, and added a task to a linked list to be processed - list_append ALSO uses
 * malloc.
 * This appears only uses malloc on startup, which is 100% OK, and never at runtime.
 *
 * Each "nethandler" (currently one for ARP, one for ICMP - which will likely be expanded to IP)
 * holds a number of buffers, of which only one is used at a time.
 */

nethandler_t *nethandler_arp = NULL;
nethandler_t *nethandler_icmp = NULL;

// Create a new nethandler, and start a task.
nethandler_t *nethandler_create(const char *name, void (*func)(void *, uint32)) {
	nethandler_t *worker = kmalloc(sizeof(nethandler_t));

	assert(strlen(name) + 1 <= NETHANDLER_NAME_SIZE);
	strlcpy(worker->name, name, NETHANDLER_NAME_SIZE);

	worker->function = func;

	// Allocate memory for the buffers, and set them to a known state
	for (int i=0; i < NETHANDLER_NUM_BUFFERS; i++) {
		worker->buffers[i] = kmalloc(sizeof(nethandler_buffer_t));
		memset(worker->buffers[i], 0, sizeof(nethandler_buffer_t)); // Also sets length and state
	}

	// Create a process to do all this
	worker->task = create_task(nethandler_task, name, &kernel_console, worker, sizeof(nethandler_t));

	return worker;
}

// Called by the network card ISR to process a packet
void nethandler_add_packet(nethandler_t *worker, void *data, uint32 length) {
	assert(worker != NULL);
	assert(data != NULL);
	assert(length > 0);

	// Find the first free buffer
	nethandler_buffer_t *buffer = NULL;
	for (int i = 0; i < NETHANDLER_NUM_BUFFERS; i++) {
		if (worker->buffers[i]->state == EMPTY) {
			buffer = worker->buffers[i];
			break;
		}
	}
	if (buffer == NULL) {
		// No free buffers: drop this packet
		return;
	}

	memcpy(buffer->buffer, data, length);
	buffer->length = length;

	// This MUST be last, since the task may start work at any time after this is set
	buffer->state = NEEDS_PROCESSING;
}

// The *process* that does all the work. I'll try to come up with better naming
// than to have "task" mean two different things...
void nethandler_task(void *data, uint32 length) {
	nethandler_t *worker = (nethandler_t *)data;

	while (true) {
		nethandler_buffer_t *buffer = NULL;
		while (buffer == NULL) {
			buffer = NULL;
			for (int i=0; i < NETHANDLER_NUM_BUFFERS; i++) {
				if (worker->buffers[i]->state == NEEDS_PROCESSING) {
					buffer = worker->buffers[i];
					buffer->state = CURRENTLY_PROCESSING;
					break;
				}
			}

			if (buffer == NULL) {
				// Nothing to do; switch to some other task, that can actually do something
				sleep(10);
				//YIELD;
			}
		}

		worker->function(buffer->buffer, buffer->length);
		memset(buffer->buffer, 0, NETHANDLER_BUFFER_SIZE);

		// Must be the very LAST thing we do, since the ISR may start filling this up any time when this is set
		buffer->state = EMPTY;
	}
}
