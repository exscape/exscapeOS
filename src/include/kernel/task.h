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
	uint8 privilege; /* this task's privilege level (i.e. 0 or 3) */
	console_t *console;
} task_t;

#define TASK_RUNNING (1 << 0)
#define TASK_SLEEPING (1 << 1)
#define TASK_IOWAIT (1 << 2)

void set_next_task(task_t *task);
bool does_task_exist(task_t *task);
void init_tasking(uint32 kerntask_esp0);
int getpid(void);
task_t *create_task( void (*entry_point)(void *, uint32), const char *name, console_t *con, void *data, uint32 length);
task_t *create_task_user( void (*entry_point)(void *, uint32), const char *name, console_t *con, void *data, uint32 length);
uint32 scheduler_taskSwitch(uint32 esp);
uint32 switch_task(task_t *new_task, uint32 esp);
bool kill_pid(int pid);
void kill(task_t *task);
void sleep(uint32 milliseconds);

/* Used in the ATA driver, to make tasks sleep while waiting for the disk to read data */
void scheduler_set_iowait(void);
uint32 scheduler_wake_iowait(uint32 esp);

#endif
