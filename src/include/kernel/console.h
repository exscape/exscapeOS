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
	uint8 text_color;
	uint8 back_color;
} console_t;

#define NUM_SCROLLBACK 15 // how many screens worth of scrollback should each console have?
#define MAX_SCROLLBACK ((24*NUM_SCROLLBACK) - 1) // how many LINES can we scroll back maximum?
#define CONSOLE_BUFFER_SIZE (80*24*(NUM_SCROLLBACK+1)) // number of CHARACTERS (one character uses 16 bits)
#define CONSOLE_BUFFER_SIZE_BYTES (CONSOLE_BUFFER_SIZE * 2) // number of BYTES, for kmalloc etc.

#include <kernel/task.h> /* must be done after defining console_t */

// 80x25 text mode color values
#define BLACK 0
#define BLUE 1
#define GREEN 2
#define CYAN 3
#define RED 4
#define MAGENTA 5
#define BROWN 6
#define LIGHT_GREY 7
#define DARK_GREY 8
#define LIGHT_BLUE 9
#define LIGHT_GREEN 10
#define LIGHT_CYAN 11
#define LIGHT_RED 12
#define LIGHT_MAGENTA 13
#define LIGHT_BROWN 14
#define WHITE 15
// Bits to shift these values to get them in place
#define BGCOLOR 12
#define FGCOLOR 8

void set_text_color(int color);
void set_back_color(int color);

/* If true, all output will be on the current console, no matter who's writing */
extern volatile bool force_current_console;

unsigned char getchar(void);

void scrollback_up(void);
void scrollback_down(void);
void scrollback_pgup(void);
void scrollback_pgdown(void);
void scrollback_reset(void);

// Called by the timer; would be static otherwise
void update_statusbar(void);

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
size_t printc(int back_color, int text_color, const char *fmt, ...); // printk with colors
size_t printk(const char *fmt, ...);
int puts(const char *s);
int puts_xy(const char *s, int x, int y);

Point get_cursor(void);
bool set_cursor(int x, int y);
void cursor_left(void);
void cursor_right(void);

#endif
