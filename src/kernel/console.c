#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <kernel/console.h>
#include <kernel/kernutil.h>
#include <kernel/kheap.h>
#include <kernel/list.h>
#include <kernel/timer.h>

/* A character representing empty space on the screen */
static const uint16 blank = (0x7 << 8 /* grey on black */) | 0x20 /* space */;

static uint16 *videoram = (uint16 *) 0xb8000;

/* Used for double buffering when scrolling (due to a lack of memmove()) */
static uint16 *vram_buffer = NULL;

/* task.c */
extern volatile task_t *current_task;
extern volatile task_t *console_task;
extern task_t kernel_task;

/* TODO: mutexes! */

volatile console_t *current_console;

static void force_update_cursor(void);
static void redraw_screen(void);

/* A set of virtual consoles, accessed using Alt+F1, Alt+F2, ..., Alt+Fn */
#define NUM_VIRTUAL_CONSOLES 4
console_t *virtual_consoles[NUM_VIRTUAL_CONSOLES];
/* These are set up properly in kmain() */

#include <kernel/task.h>

/* I'm NOT happy about this mess, but it really should be set up statically...
   ... yet isn't; part is in kmain(), which dynamically allocates part of the
   kernel_console struct. */
extern list_t kernel_console_tasks;
static node_t tmp = {
	.next = NULL,
	.prev = NULL,
	.list = &kernel_console_tasks,
	.data = &kernel_task
};
list_t kernel_console_tasks = {
	.head = &tmp,
	.tail = &tmp,
	.count = 1
};

console_t kernel_console = {
	.tasks = &kernel_console_tasks,
	.active = true,
	.cursor = { .x = 0, .y = 0},
};

/* Returns a key from the keyboard buffer, if possible. */
unsigned char getchar(void) {
	/* If no characters are available, loop until there's something. */
	assert(current_task->console != NULL);

	volatile struct ringbuffer *keybuffer = (volatile struct ringbuffer *) & current_task->console->keybuffer;
	while (keybuffer->counter == 0) {
		sleep(10);
	}

	assert(keybuffer->counter != 0);
	assert(keybuffer->read_ptr != NULL);
	unsigned char ret = *(keybuffer->read_ptr++);
	keybuffer->counter--;
	if (keybuffer->read_ptr > keybuffer->data + KEYBUFFER_SIZE) // TODO: OBOE?
		keybuffer->read_ptr = keybuffer->data;

	return ret;
}

// NOTE: these only do half the work... they may well point outside of the buffer!
// TODO: fix this.
// cur_visible: what part of the screen is visible, period - possibly in scrollback
// TODO: these had NUM_SCROLLBACK-1 previously, which mostly worked as well - WHY?
// Wouldn't that fail horribly?
#define cur_visible(_con) ( (uint16 *)(_con->bufferptr + 80*25*(NUM_SCROLLBACK) - 80*(_con->current_position)) )
// cur_screen: the "current" 80x25 screen. Part (or all of it) may be undisplayed due to scrollback.
#define cur_screen(_con) ( (uint16 *)(_con->bufferptr + 80*25*(NUM_SCROLLBACK)) )

void console_switch(console_t *new) {
	assert(new != NULL);

	if (new == current_console) {
		return;
	}

	current_console->active = false;
	new->active = true;
	current_console = new;
	redraw_screen();
	force_update_cursor();

	//panic("test console_switch() Can this use redraw_screen()?!");

	/*
	// Copy the video memory of the new console to the actual video RAM, to display it
	uint16 *cur_vis = cur_visible(new);
	if (cur_vis >= new->buffer + CONSOLE_BUFFER_SIZE) {
		cur_vis = new->buffer + (cur_vis - (new->buffer + CONSOLE_BUFFER_SIZE));
	}

	if (cur_vis + 80*25 >= new->buffer + CONSOLE_BUFFER_SIZE) {
		// Copy part one: from somewhere in the "middle" to the end of the buffer
		panic("FIXME");
		uint32 copied = (new->buffer + CONSOLE_BUFFER_SIZE) - cur_vis;
		memcpy(videoram, cur_vis, copied * 2); // *2 to convert to bytes
		// Part two: one screen worth, minus the amount we already copied
		memcpy(((uint8 *)videoram) + copied, new->buffer, (80*25*2) - (copied*2));
	}
	else
		memcpy(videoram, cur_vis, 80*25*2);

	// Switch the consoles
	current_console->active = false;
	new->active = true;
	current_console = new;
	force_update_cursor();
	*/
}

/* Creates a new console for the specified task */
#if 1
console_t *console_create(void) {

	/* This function should only be used to set up the virtual consoles,
	 * at the moment! If the last of them is set up, well... */
	assert(virtual_consoles[NUM_VIRTUAL_CONSOLES - 1] == NULL);

	console_t *new = kmalloc(sizeof(console_t));
	memset(new, 0, sizeof(console_t));

	/* Set the new console up. This is done separately so that non-malloc'ed consoles
	 * can also be set up. */
	console_init(new);

	return new;
}
#endif

/* Initalize a console. Used during console creation; only called manually for static console_t's */
void console_init(console_t *new) {
	new->active = false;

	/* Set up the keyboard ring buffer for this console */
	new->keybuffer.read_ptr = new->keybuffer.data;
	new->keybuffer.write_ptr = new->keybuffer.data;
	new->keybuffer.counter = 0;

	/* Set up the tasks list. kmalloc should be available any time this is called */
	assert(new->tasks == NULL);
	new->tasks = list_create();

	new->buffer = kmalloc(CONSOLE_BUFFER_SIZE_BYTES);
	if (new->buffer == 0)
		panic("temp");
	//new->bufferptr = new->buffer;
	//new->current_position = 0;

	/* Copy the screen content and cursor position from the currently displayed console */
	assert(current_console->buffer != NULL);
	memcpy(new->buffer, ((console_t *)current_console)->buffer, CONSOLE_BUFFER_SIZE_BYTES);
	new->bufferptr = new->buffer + (current_console->bufferptr - current_console->buffer);
	new->current_position = current_console->current_position;
	memcpy(& new->cursor, & ((console_t *)current_console)->cursor, sizeof(Point));
}

/* Destroy a console (free its memory, etc.) and switch to the previous one */
#if 0
void console_destroy(console_t *con) {
	assert(con->prev_console != NULL);
	console_t *prev = con->prev_console;

	/* Copy the new display back to the previous console */
	memcpy(prev->videoram, con->videoram, 80*25*2);
	memcpy(&prev->cursor, &con->cursor, sizeof(Point));

	if (current_console == con) {
		console_switch(con->prev_console);
	}

	kfree(con);
}
#endif

/* Syscall test function */
int puts(const char *s) {
	size_t len = strlen(s);

	for (size_t j = 0; j < len; j++) {
		putchar(s[j]);
	}

	update_cursor();

	return len;
}
/*
void print_time(const Time *t) {
	// Prints the time in the bottom corner.

	// Save the cursor (since printk will modify it)
	Point p = cursor;

	cursor.y = 24;
	cursor.x = 0;

	// clear the area
	memsetw(videoram + 24*80, blank, 80);

	printk("%d-%02d-%02d %02d:%02d:%02d", 
			t->year, t->month, t->day, t->hour, t->minute, t->second);

	// Restore the cursor
	cursor = p;
}
*/

/* Called by kmain() on boot. Creates the VRAM buffer (used in scroll() and maybe others). */
void init_video(void) {
	kernel_task.console = &kernel_console;
	current_console = &kernel_console;

	if (vram_buffer == NULL)
		vram_buffer = (uint16 *)kmalloc(80*25*2);

	clrscr();
}

void clrscr(void) {
	//panic("TODO: clrscr() -- essientially, print 25 blank lines (due to scrollback)?");
	for (int i=0; i<25; i++)
		printk("\n"); // TODO: FIXME! Not very ideal! Besides, the cursor ends up at the bottom.
/*
	assert(console_task->console != NULL);
	memsetw(console_task->console->videoram, blank, 80*25);

	if (list_find_first(current_console->tasks, (void *)console_task) != NULL) {
		// If the task that's calling clrscr() has its console on display, also update the screen at once
		memsetw(videoram, blank, 80*25);
	}

	Point *cursor = &console_task->console->cursor;
	cursor->x = 0;
	cursor->y = 0;
	update_cursor();
*/
}

void cursor_left(void) {
	assert(console_task->console != NULL);
	Point *cursor = &console_task->console->cursor;
	if (cursor->x != 0)
		cursor->x--;

	update_cursor();
}

void cursor_right(void) {
	assert(console_task->console != NULL);
	Point *cursor = &console_task->console->cursor;
	if (cursor->x < 79)
		cursor->x++;
	else {
		cursor->x = 0;
		cursor->y++;
		scroll();
	}

	update_cursor();
}

static void print_scrollback_pos(void) {
	// Temporary! TODO FIXME
	char buf[16] = {0};
	sprintf(buf, "%u", current_console->current_position);
	puts_xy(buf, 76, 0);
	puts_xy("   ", 76, 1);
}

void scrollback_up(void) {
	if (current_console == NULL)
		panic("scrollback_up without a console - fix this panic!");

	if (current_console->current_position >= MAX_SCROLLBACK) {
		return;
	}

	current_console->current_position++;
	redraw_screen();
	print_scrollback_pos();
}

void scrollback_pgup(void) {
	if (current_console == NULL)
		panic("scrollback_up without a console - fix this panic!");

	if (current_console->current_position >= MAX_SCROLLBACK) {
		return;
	}
	else if (current_console->current_position + 25 >= MAX_SCROLLBACK) {
		current_console->current_position = MAX_SCROLLBACK;
	}
	else
		current_console->current_position += 25;

	redraw_screen();
	print_scrollback_pos();
}

void scrollback_down(void) {
	if (current_console == NULL)
		panic("scrollback_down without a console - fix this panic!");

	if (current_console->current_position == 0) {
		return;
	}

	current_console->current_position--;
	redraw_screen();
	print_scrollback_pos();
}

void scrollback_pgdown(void) {
	if (current_console == NULL)
		panic("scrollback_up without a console - fix this panic!");

	if (current_console->current_position == 0) {
		return;
	}
	else if (current_console->current_position < 25) {
		current_console->current_position = 0;
	}
	else
		current_console->current_position -= 25;

	redraw_screen();
	print_scrollback_pos();
}

void scrollback_reset(void) {
	if (current_console == NULL)
		panic("scrollback_reset without a console - fix this panic!");

	current_console->current_position = 0;
	redraw_screen();
	print_scrollback_pos();
}

// Copies the part of the screen that should be visible from the scrollback
// buffer to both the VRAM buffer and the actual video RAM
static void redraw_screen(void) {

	uint16 *cur_vis = cur_visible(current_console);
	if (cur_vis >= current_console->buffer + CONSOLE_BUFFER_SIZE) {
		cur_vis = current_console->buffer + (cur_vis - (current_console->buffer + CONSOLE_BUFFER_SIZE));
	}

	if (cur_vis + 80*25 >= current_console->buffer + CONSOLE_BUFFER_SIZE) {
		// Copy part one
		uint32 copied = (current_console->buffer + CONSOLE_BUFFER_SIZE) - cur_vis;
		memcpy(vram_buffer, cur_vis, copied * 2);
		// Part two
		memcpy(((uint8 *)vram_buffer) + (copied*2), current_console->buffer, (80*25 - copied)*2);
	}
	else {
		memcpy(vram_buffer , cur_vis, 80*25*2);
	}
	memcpy(videoram, vram_buffer, 80*25*2);

	// TODO: keep multiple tasks and multiple consoles in mind!
#if 0
	// Also update the screen, if this console is currently displayed
	if (list_find_first(current_console->tasks, (void *)console_task) != NULL) {
		assert(current_console->active == true);
		cur_vis = cur_visible(console_task->console);
		if (cur_vis >= console_task->console->buffer + CONSOLE_BUFFER_SIZE) {
			cur_vis = console_task->console->buffer + (cur_vis - (console_task->console->buffer + CONSOLE_BUFFER_SIZE));
		}
		if (cur_vis + 80*25 >= console_task->console->buffer + CONSOLE_BUFFER_SIZE) {
			uint32 copied = (console_task->console->buffer + CONSOLE_BUFFER_SIZE) - cur_vis;
			memcpy(videoram, cur_vis, copied*2);
			memcpy(((uint8 *)videoram) + (copied*2), console_task->console->buffer, (80*25*2) - (copied*2));
		}
		else {
			memcpy(videoram, cur_vis, 80*25*2);
		}
	}
#endif
}

void scroll(void) {
	if (console_task->console == NULL)
		panic("scroll() in task without a console!");

	Point *cursor = &console_task->console->cursor;
	if (cursor->y < 25)
		return;

	if (console_task->console->current_position != 0) {
		// We're in scrollback at the moment
		if (console_task->console->current_position == MAX_SCROLLBACK) {
			// We're scrolled back too far - a line on screen will be overwritten
			console_task->console->current_position--;
			//redraw_screen(); // probably shouldn't redraw until the rest is done, or there will be two "jerks" for one line printed
		}
		else {
			// In scrollback, but not maximum. Since a new line has entered the screen,
			// we're now one line further back in the buffer.
			console_task->console->current_position++;
		}
	}

	// Move forward one line in the scrollback buffer, which causes the last line to "fall out"
	// Also, handle wrapping (this is a ring buffer)
	current_console->bufferptr += 80;
	if (current_console->bufferptr >= current_console->buffer + CONSOLE_BUFFER_SIZE) {
		if (!(current_console->bufferptr - (current_console->buffer + CONSOLE_BUFFER_SIZE) == 0)) {
			// Ugly, temporary assert! TODO
			memsetw(videoram, 0xfabc, 80*2);
			while(true) { }
		}
		current_console->bufferptr = current_console->buffer;
	}

	// Blank the last line...
	//assert(cur_visible(current_console) + 80*24 + 80 < current_console->buffer + CONSOLE_BUFFER_SIZE);
	if ((cur_screen(current_console) + 80*24 + 80 <= current_console->buffer + CONSOLE_BUFFER_SIZE)) {
		memsetw(cur_screen(current_console) + 80*24, blank, 80); // no wrap trouble, one line always fits
	}
	else {
		uint32 offset = (cur_screen(current_console) + 80*24) - (current_console->buffer + CONSOLE_BUFFER_SIZE);
		memsetw(current_console->buffer + offset, blank, 80);
	}

	if (console_task->console->current_position == 0) {
		redraw_screen();
	}
	/*
	else {
		// This console is in scrollback mode. redraw_screen() will only work properly when
		// the screen is in "normal" display mode.
		uint16 *cur_scr = cur_screen(current_console);
		if (cur_scr >= current_console->buffer + CONSOLE_BUFFER_SIZE) {
			cur_scr = current_console->buffer + (cur_scr - (current_console->buffer + CONSOLE_BUFFER_SIZE));
		}
	*/
		/*
		if (cur_scr + 80*25 >= current_console->buffer + CONSOLE_BUFFER_SIZE) {
			// Copy part one
			uint32 copied = (current_console->buffer + CONSOLE_BUFFER_SIZE) - cur_scr;
			memcpy(vram_buffer, cur_scr, copied * 2);
			// Part two
			memcpy(((uint8 *)vram_buffer) + (copied*2), current_console->buffer, (80*25*2) - (copied*2));
		}
		else {
			memcpy(vram_buffer , cur_scr, 80*25*2);
		}
		memcpy(videoram, vram_buffer, 80*25*2); // TODO: this + vram_buffer update shouldn't be needed - in MOST cases, nothing
		// should change. Think this through.

		*/
	// Move the cursor
	cursor->y = 24;
//	update_cursor();

//	return;
	/*
	// and draw it to the buffer, keeping in mind that we might need to wrap around
	uint16 *cur_vis = cur_visible(current_console);
	if (cur_vis >= current_console->buffer + CONSOLE_BUFFER_SIZE) {
		cur_vis = current_console->buffer + (cur_vis - (current_console->buffer + CONSOLE_BUFFER_SIZE));
	}

	if (cur_vis + 80*25 >= current_console->buffer + CONSOLE_BUFFER_SIZE) {
		// Copy part one
		//panic("FIXME");
		uint32 copied = (current_console->buffer + CONSOLE_BUFFER_SIZE) - cur_vis;
		memcpy(vram_buffer, cur_vis, copied * 2);
		// Part two
		memcpy(((uint8 *)vram_buffer) + (copied*2), current_console->buffer, (80*25*2) - (copied*2));
	}
	else {
		memcpy(vram_buffer , cur_vis, 80*25*2);
	}

	// Also update the screen, if this console is currently displayed
	if (list_find_first(current_console->tasks, (void *)console_task) != NULL) {
		assert(current_console->active == true);
		cur_vis = cur_visible(console_task->console);
		if (cur_vis >= console_task->console->buffer + CONSOLE_BUFFER_SIZE) {
			cur_vis = console_task->console->buffer + (cur_vis - (console_task->console->buffer + CONSOLE_BUFFER_SIZE));
		}
		if (cur_vis + 80*25 >= console_task->console->buffer + CONSOLE_BUFFER_SIZE) {
			uint32 copied = (console_task->console->buffer + CONSOLE_BUFFER_SIZE) - cur_vis;
			memcpy(videoram, cur_vis, copied*2);
			memcpy(((uint8 *)videoram) + (copied*2), console_task->console->buffer, (80*25*2) - (copied*2));
		}
		else {
			memcpy(videoram, cur_vis, 80*25*2);
		}
	}
	*/

/*
	// Copy the entire screen to the buffer
	memcpy(vram_buffer, cur_visible(console_task->console), 80*25*2);

	// Copy back the lower 24 lines
	// Note that we add 80, not 80*2, due to pointer arithmetic! (vram_buffer is a uint16 *)
	memcpy(console_task->console->videoram, vram_buffer + 80, 80*24*2);

	// Blank the last line
	memsetw(console_task->console->videoram + 24*80, blank, 80);

	// Also update the screen, if this console is currently displayed
	if (list_find_first(current_console->tasks, (void *)console_task) != NULL) {
		assert(current_console->active == true);
		memcpy(videoram, console_task->console->videoram, 80*25*2);
	}
	*/

}

// Prints a string directly to VRAM.
// Mostly useful for console development/debugging, since it will be easily
// overwritten by stuff.
int puts_xy(const char *str, int x, int y) {
	assert(str != NULL && (x >= 0 && x <= 79) && (y >= 0 && y <= 24));
	for (size_t i=0; i < strlen(str); i++) {
		const unsigned int offset = y*80 + x + i;
		videoram[offset] = ((unsigned char)str[i]) | (0x07 << 8);
	}
	return strlen(str);
}

int putchar(int c) {
	Point *cursor = NULL;

	assert(console_task->console != NULL);

	cursor = &console_task->console->cursor;
	assert(cursor != NULL);

	if (c == '\n') {
		// c == newline
		cursor->x = 0;
		cursor->y++;
	}
	else if (c == 0x08) {
		// Backspace
		if (cursor->x > 0)
			cursor->x--;
	}
	else if (c >= 0x20) {
		// 0x20 is the lowest printable character (space)
		// Write the character
		const unsigned int offset = cursor->y*80 + cursor->x;
		if (console_task->console != NULL) {
			uint16 *addr = cur_screen(console_task->console) + offset;
			if (addr >= console_task->console->buffer + CONSOLE_BUFFER_SIZE) {
				addr = console_task->console->buffer + (addr - (console_task->console->buffer + CONSOLE_BUFFER_SIZE));
			}

			*addr = ( ((unsigned char)c)) | (0x07 << 8); /* grey on black */
			//console_task->console->videoram[offset] = ( ((unsigned char)c)) | (0x07 << 8); /* grey on black */

			//if (console_task->console->current_position != 0) {
				//if (console_task->console->current_position != MAX_SCROLLBACK) {
					//console_task->console->current_position++;
					//// TODO: does this automatically scroll down if you're at max scrollback?
				//}
			//}
		}

		if (list_find_first(current_console->tasks, (void *)console_task) != NULL) {
			/* Also update the actual video ram if this console is currently displayed */
			if (console_task->console->current_position == 0) { // TODO: FIXME: there are other cases where we're scrolled but it's still visible
				// but at an offset
				/* ... but only if we're not scrolled back past this */
				videoram[offset] = ( ((unsigned char)c)) | (0x07 << 8); /* grey on black */
			}
		}

		if (cursor->x + 1 == 80) {
			// Wrap to the next line
			cursor->y++;
			cursor->x = 0;
		}
		else {
			// Don't wrap
			cursor->x++;
		}
	}

	scroll(); // Scroll down, if need be
//	update_cursor();

	return c;
}

void update_cursor(void) {
	// Moves the hardware cursor to the current position specified by the cursor struct

	if (list_find_first(current_console->tasks, (void *)console_task) == NULL) {
		/* The current task (console_task) isn't among this console's tasks.
		 * Don't update the cursor on screen now. */
		return; 
	}

	assert(console_task->console == current_console);

	Point *cursor = & ((console_t *)current_console)->cursor;
	uint16 loc = cursor->y * 80 + cursor->x;

	uint8 high = (uint8)((loc >> 8) & 0xff);
	uint8 low  = (uint8)(loc & 0xff);
	outb(0x3d4, 0xe);
	outb(0x3d5, high);
	outb(0x3d4, 0xf);
	outb(0x3d5, low);
}

/* Ugh, code duplication... */
static void force_update_cursor(void) {
	assert(current_console != NULL);
	Point *cursor = & ((console_t *)current_console)->cursor;
	assert(cursor != NULL);

	uint16 loc = cursor->y * 80 + cursor->x;

	uint8 high = (uint8)((loc >> 8) & 0xff);
	uint8 low  = (uint8)(loc & 0xff);
	outb(0x3d4, 0xe);
	outb(0x3d5, high);
	outb(0x3d4, 0xf);
	outb(0x3d5, low);
}

/* The buffer used by printk */
static char buf[1024];

size_t printk(const char *fmt, ...) {
	/*
	static int line = 1; // TODO
	sprintf(buf, "%u\n", line++);
	if (line >= 122)
		delay(10);
	for (size_t j = 0; j < strlen(buf); j++)
		putchar(buf[j]);
	fmt += 1;
	update_cursor();
	//if (interrupts_enabled)
	//delay(500);
	return strlen(buf);
	*/


	va_list args;
	int i;

	va_start(args, fmt);
	i = vsprintf(buf, fmt, args);
	va_end(args);

	if (i > 0) {
		size_t len = strlen(buf);
		for (size_t j = 0; j < len; j++) {
			putchar(buf[j]);
		}
	}
	update_cursor();

	return i;
}

int sprintf(char *sprintf_buf, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vsprintf(sprintf_buf,fmt,args);
	va_end(args);

	return i;
}
