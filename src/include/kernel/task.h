#ifndef _TASK_H
#define _TASK_H

#include <kernel/paging.h>
#include <types.h>

typedef struct task
{
   int id;                // Process ID.
   uint32 esp, ebp;       // Stack and base pointers.
   uint32 ss;
   uint32 eip;            // Instruction pointer.
   void *stack; // This task's kernel stack
   page_directory_t *page_directory; // Page directory.
   struct task *next;     // The next task in a linked list.
} task_t;

void init_tasking(uint32 kerntask_esp0);
int getpid(void);
void switch_to_user_mode(void);
task_t *create_task( void (*entry_point)(void) );
uint32 scheduler_taskSwitch(uint32 esp);
uint32 switch_task(task_t *new_task);

#endif
