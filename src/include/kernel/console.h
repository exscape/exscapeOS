#ifndef _CONSOLE_H
#define _CONSOLE_H
#include <types.h>
#include <kernel/keyboard.h>

/* A ring buffer that stores keystrokes to a console */
typedef struct ringbuffer {
	volatile unsigned char data[KEYBUFFER_SIZE];
	volatile unsigned char *read_ptr;
	volatile unsigned char *write_ptr; /* volatile is probably not needed */
	volatile uint32 counter; /* how much unread data is stored? */
} ringbuffer_t;

typedef struct task task_t;
/* Defines a single virtual console */
typedef struct console {
	task_t *task;
	bool active;
	uint16 videoram[80 * 25];
	Point cursor;
	struct console *prev_console;
	volatile struct ringbuffer keybuffer;
} console_t;
#include <kernel/task.h> /* must be done after defining console_t */

/* If true, all output will be on the current console, no matter who's writing */
extern volatile bool force_current_console;

unsigned char getchar(void);

extern volatile console_t *current_console;
extern console_t kernel_console;

#define NUM_VIRTUAL_CONSOLES 4
extern console_t virtual_consoles[NUM_VIRTUAL_CONSOLES];

void console_destroy(console_t *con);
console_t *console_create(void);
void console_init(console_t *new);
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
