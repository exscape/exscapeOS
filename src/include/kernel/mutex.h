#ifndef _MUTEX_H
#define _MUTEX_H

struct task; // Forward declare instead of including task.h
			 // since that causes problems

typedef struct _mutex {
	/*task_t *owner;*/
	struct task *owner;
	uint32 mutex;
} mutex_t;

mutex_t *mutex_create(void);
void mutex_destroy(mutex_t *mutex);

void mutex_lock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);
bool mutex_is_locked(mutex_t *mutex);

#endif
