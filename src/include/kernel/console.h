#ifndef _CONSOLE_H
#define _CONSOLE_H
#include <types.h>

typedef struct task task_t;
/* Defines a single virtual console */
typedef struct console {
	task_t *task;
	bool active;
	uint16 *videoram[80 * 25];
	Point cursor;
} console_t;

#include <kernel/task.h>

extern volatile console_t *current_console;
extern console_t kernel_console;

int putchar(int c);
void init_video(void);
void clrscr(void);
void update_cursor(void);
void scroll(void);
void print_time(const Time *t);
size_t printk(const char *fmt, ...);
int print(const char *s);

void cursor_left(void);
void cursor_right(void);

#endif
