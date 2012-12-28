#ifndef _TASK_H
#define _TASK_H

#include <kernel/vmm.h>
#include <sys/types.h>
#include <kernel/console.h>
#include <kernel/vfs.h> /* struct open_file */
#include <kernel/heap.h>
#include <reent.h>

#define TASK_NAME_LEN 64

typedef struct task {
	int id;                // Process ID.
	char name[TASK_NAME_LEN];
	uint32 esp;
	uint32 ss;
	void *stack; // This task's kernel stack
	uint32 state; /* e.g. running, sleeping */
	uint32 wakeup_time; /* for sleeping tasks only: at which tick this task should be woken */
	uint8 privilege; /* this task's privilege level (i.e. 0 or 3) */
	console_t *console;
	struct task_mm *mm; /* memory info, including the page directory pointer */
	struct open_file **fdtable;
	char *pwd;
	struct _reent *reent; // Used by Newlib

	struct task *parent;
	list_t *children;
	int exit_code;
} task_t;

extern volatile task_t *current_task;
extern volatile task_t *console_task;
extern task_t kernel_task;

#define TASK_RUNNING (1 << 0)
#define TASK_SLEEPING (1 << 1)
#define TASK_WAKING_UP (TASK_RUNNING | TASK_SLEEPING) // was sleeping, has not yet been scheduled since then
#define TASK_IOWAIT (1 << 2)
#define TASK_EXITING (1 << 3)
#define TASK_IDLE (1 << 4) // used for the idle_task process
#define TASK_DEAD (1 << 5) // task has _exit()'ed, but has not been wait()ed on
#define TASK_WAITING (1 << 6) // wait()ing for a child

void user_exit(void); // called from user mode

#define USER_STACK_START 0xbffff000
#define USER_STACK_SIZE 16384 /* overkill? */

char **parse_command_line(const char *cmdline, uint32 *argc, task_t *task);
void set_entry_point(task_t *task, uint32 addr);
void set_next_task(task_t *task);
bool does_task_exist(task_t *task);
void init_tasking(uint32 kerntask_esp0);
int getpid(void);
task_t *create_task_elf(const char *path, console_t *con, void *data, uint32 length);
task_t *create_task( void (*entry_point)(void *, uint32), const char *name, console_t *con, void *data, uint32 length);
task_t *create_task_user( void (*entry_point)(void *, uint32), const char *name, console_t *con, void *data, uint32 length);
uint32 scheduler_taskSwitch(uint32 esp);
uint32 switch_task(task_t *new_task, uint32 esp);
bool kill_pid(int pid); /* calls kill on the correct task */
void kill(task_t *task); /* sets a task to TASK_EXITING so that it never runs */
void destroy_task(task_t *task); /* actually kills the task for good */
void sleep(uint32 milliseconds);
int fork(void);

/* Used in the ATA driver, to make tasks sleep while waiting for the disk to read data */
void scheduler_set_iowait(void);
uint32 scheduler_wake_iowait(uint32 esp);

#endif
