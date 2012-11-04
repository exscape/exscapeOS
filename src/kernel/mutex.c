#include <kernel/kheap.h>
#include <kernel/task.h>
#include <kernel/mutex.h>
#include <kernel/kernutil.h>
#include <kernel/console.h> // TODO: debugging only!

mutex_t *mutex_create(void) {
	mutex_t *mutex = kmalloc(sizeof(mutex_t));
	mutex->owner = NULL;
	mutex->mutex = 0;

	return mutex;
}

extern volatile bool in_isr;
extern volatile bool task_switching;

void mutex_destroy(mutex_t *mutex) {
	assert(mutex->mutex == 0); // Must not be locked
	assert(mutex->owner == NULL); // Set during unlock
	kfree(mutex);
}

void mutex_lock(mutex_t *mutex) {
	assert(mutex != NULL);
	//printk("mutex_lock(%p)\n", mutex);
	//if (in_isr)
	//panic("mutex_lock from ISR!");
	uint8 success = 0;

	while (success == 0) {
		asm volatile("LOCK BTSL $0, %[mutex];"
					 "SETNCB %[success];"
					 :
					  [mutex]"=m"(mutex->mutex),
					  [success]"=m"(success)
					  : : "cc", "memory");


		if (success) {
			mutex->owner = (task_t *)current_task;
			//printk("Mutex locked\n");
		}
		else {
			//printk("Mutex not locked!");
			if (task_switching)
				asm volatile("int $0x7e");
		}
	}
}

void mutex_unlock(mutex_t *mutex) {
	assert(mutex != NULL);
	assert(mutex->mutex != 0); // mutex is locked
	//printk("mutex_unlock(%p)\n", mutex);
	//if (in_isr)
	//panic("mutex_unlock from ISR!");
	assert(mutex->owner == current_task);
	mutex->mutex = 0;
	mutex->owner = NULL;
	//printk("Mutex unlocked\n");
}

bool mutex_is_locked(mutex_t *mutex) {
	return mutex->mutex != 0;
}
