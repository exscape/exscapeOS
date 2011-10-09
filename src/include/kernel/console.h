#ifndef _CONSOLE_H
#define _CONSOLE_H
#include <types.h>

typedef struct task task_t;
/* Defines a single virtual console */
typedef struct console {
	task_t *task;
	bool active;
	uint16 videoram[80 * 25];
	Point cursor;
	struct console *prev_console;
} console_t;

#include <kernel/task.h>

extern volatile console_t *current_console;
extern console_t kernel_console;

#define NUM_VIRTUAL_CONSOLES 4
extern console_t virtual_consoles[NUM_VIRTUAL_CONSOLES];

void console_destroy(console_t *con);
console_t *console_create(void);
void console_switch(console_t *new);

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
