#ifndef _CONSOLE_H
#define _CONSOLE_H
#include <types.h>
#include <kernel/keyboard.h>
#include <kernel/list.h>

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
	list_t *tasks;
	bool active;
	Point cursor;
	volatile struct ringbuffer keybuffer;
	uint16 *buffer; // ring buffer for scrollback + onscreen data
	uint16 *bufferptr; // pointer to the current "start" of the ring buffer
	uint16 current_position; // how many lines have we scrolled back?
} console_t;

#define NUM_SCROLLBACK 3 // how many screens worth of scrollback should each console have?
#define CONSOLE_BUFFER_SIZE (80*25*(NUM_SCROLLBACK+1)) // number of CHARACTERS (one character uses 16 bits)
#define CONSOLE_BUFFER_SIZE_BYTES (CONSOLE_BUFFER_SIZE * 2) // number of BYTES, for kmalloc etc.

#include <kernel/task.h> /* must be done after defining console_t */

/* If true, all output will be on the current console, no matter who's writing */
extern volatile bool force_current_console;

unsigned char getchar(void);

extern volatile console_t *current_console;
extern console_t kernel_console;

#define NUM_VIRTUAL_CONSOLES 4
extern console_t *virtual_consoles[NUM_VIRTUAL_CONSOLES];

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
int puts(const char *s);

void cursor_left(void);
void cursor_right(void);

#endif
