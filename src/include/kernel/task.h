#ifndef _TASK_H
#define _TASK_H

#include <kernel/paging.h>
#include <types.h>

typedef struct task
{
   int id;                // Process ID.
   uint32 esp, ebp;       // Stack and base pointers.
   uint32 eip;            // Instruction pointer.
   uint32 stack; // This task's kernel stack
   page_directory_t *page_directory; // Page directory.
   struct task *next;     // The next task in a linked list.
} task_t;


void init_tasking(void);
void switch_task(void);
int getpid(void);
void switch_to_user_mode(void);
task_t *create_task( void (*entry_point)(void) );

#endif
