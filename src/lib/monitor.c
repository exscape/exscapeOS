#include <kernel/monitor.h>
#include <string.h>
#include <stdlib.h>
#include <kernel/kernutil.h>
#include <stdio.h>
#include <stdarg.h>
#include <kernel/kheap.h> /* kmalloc; TODO FIXME */

/* A character representing empty space on the screen */
static const uint16 blank = (0x7 << 8 /* grey on black */) | 0x20 /* space */;

static uint16 *videoram = (uint16 *) 0xb8000;
Point cursor;

/* Used for double buffering when scrolling (due to a lack of memmove())*/
uint16 *vram_buffer = NULL;


void *memsetw(void *dst, int val, size_t count)
{
	unsigned short *temp = (unsigned short *)dst;

	for( ; count != 0; count--)
		*temp++ = val;
	return dst;
}

void print_time(const Time *t) {
	// Prints the time in the bottom corner.

	/* Guard to make sure we only print once per update */
	static unsigned char old_sec = 255;
	if (old_sec == t->second)
		return;
	old_sec = t->second;

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

/* Called by kmain() on boot. Creates the VRAM buffer (used in scroll() and maybe others). */
void init_video(void) {
	if (vram_buffer == NULL)
		vram_buffer = (uint16 *)kmalloc(80*25*2);

	clrscr();
}

void clrscr(void) {
	memsetw(videoram, blank, 80*25);

	cursor.x = 0;
	cursor.y = 0;
	update_cursor();
}

void scroll(void) {
	if (cursor.y < 25)
		return;

	/* Copy the entire screen to the buffer */
	memcpy(vram_buffer, videoram, 80*25*2);
	
	/* Copy back the lower 24 lines
	 * Note that we add 80, not 80*2, due to pointer arithmetic! (vram_buffer is a uint16 *) */
	memcpy(videoram, vram_buffer + 80, 80*24*2);

	/* Blank the last line */
	memsetw(videoram + 24*80, blank, 80);

	/* Move the cursor */
	cursor.y = 24;
//	update_cursor();
}

int putchar(int c) {
	if (c == '\n') {
		// c == newline
		cursor.x = 0;
		cursor.y++;
	}
	else if (c == 0x08) {
		// Backspace
		if (cursor.x > 0)
			cursor.x--;
	}
	else if (c >= 0x20) {
		// 0x20 is the lowest printable character (space)
		// Write the character
		const unsigned int offset = cursor.y*80 + cursor.x;
		videoram[offset] = ( ((unsigned char)c)) | (0x07 << 8); /* grey on black */

		if (cursor.x + 1 == 80) {
			// Wrap to the next line
			cursor.y++;
			cursor.x = 0;
		}
		else {
			// Don't wrap
			cursor.x++;
		}
	}

	scroll(); // Scroll down, if need be
//	update_cursor();

	return c;
}

void update_cursor(void) {
	// Moves the hardware cursor to the current position specified by the cursor struct
	uint16 loc = cursor.y * 80 + cursor.x;

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
	va_list args;
	int i;

	va_start(args, fmt);
	i=vsprintf(buf, fmt, args);
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
	i=vsprintf(sprintf_buf,fmt,args);
	va_end(args);

	return i;
}
