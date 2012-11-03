#include <kernel/kheap.h>
#include <kernel/task.h>
#include <kernel/mutex.h>
#include <kernel/console.h> // TODO: debugging only!

mutex_t *mutex_create(void) {
	mutex_t *mutex = kmalloc(sizeof(mutex_t));
	mutex->owner = NULL;
	mutex->mutex = 0;

	return mutex;
}

void mutex_lock(mutex_t *mutex) {
	// LOCK BTS, SETC byte [success]
	printk("mutex_lock(%p)\n", mutex);
	uint8 success = 0;

	while (success == 0) {
		asm volatile("LOCK BTSL $0, %[mutex];"
					 "SETCB %[success];"
					 :
					  [mutex]"=m"(mutex->mutex),
					  [success]"=m"(success)
					  : : "cc", "memory");


		if (success) {
			mutex->owner = (task_t *)current_task;
			printk("Mutex locked\n");
		}
		else {
			printk("Mutex not locked!");
			asm volatile("int $0x7e");
		}
	}
}

void mutex_unlock(mutex_t *mutex) {
	printk("mutex_unlock(%p)\n", mutex);
}
