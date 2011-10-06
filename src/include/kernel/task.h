#ifndef _TASK_H
#define _TASK_H

#include <kernel/paging.h>
#include <types.h>
#include <kernel/console.h>

#define TASK_NAME_LEN 64

typedef struct task
{
	int id;                // Process ID.
	char name[TASK_NAME_LEN];
	uint32 esp;
	uint32 ss;
	uint32 eip;            // Instruction pointer.
	void *stack; // This task's kernel stack
	page_directory_t *page_directory; // Page directory.
	uint32 state; /* e.g. running, sleeping */
	uint32 wakeup_time; /* for sleeping tasks only: at which tick this task should be woken */
	struct task *next;     // The next task in a linked list.
	console_t *console;
} task_t;

#define TASK_RUNNING (1 << 0)
#define TASK_SLEEPING (1 << 1)

void init_tasking(uint32 kerntask_esp0);
int getpid(void);
void switch_to_user_mode(void);
task_t *create_task( void (*entry_point)(void), const char *name);
uint32 scheduler_taskSwitch(uint32 esp);
uint32 switch_task(task_t *new_task);
bool kill_pid(int pid);
void kill(task_t *task);
void sleep(uint32 milliseconds);

#endif
