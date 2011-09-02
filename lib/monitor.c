#include <monitor.h>
#include <string.h>
#include <stdlib.h>
#include <kernutil.h>

static unsigned char *videoram = (unsigned char *) 0xb8000;
static Point cursor;

void print_time(const Time *t) {
	// Prints the time in the bottom corner.

	static unsigned char old_sec = 255;
	if (old_sec == t->second)
		return;
	old_sec = t->second;

	// Since print() is the easy way...
	Point p;
	// TODO: memcpy()
	p.x = cursor.x;
	p.y = cursor.y;

	cursor.y = 24;
	cursor.x = 0;

	// clear the area
	memset(videoram + 24*80*2, 0, 40);

	char buf[16] = {0};

	/* This is going to be very ugly! */
	
	itoa(t->year, buf);
	print(buf);
	print("-");
	itoa(t->month, buf);
	print(buf);
	print("-");
	itoa(t->day, buf);
	print(buf);
	print(" ");
	
	itoa(t->hour, buf);
	print(buf);
	print(":");

	itoa(t->minute, buf);
	print(buf);
	print(":");

	itoa(t->second, buf);
	print(buf);

	// Restore the cursor
	cursor.x = p.x;
	cursor.y = p.y;
}


void clrscr(void) {
	memset(videoram, 0, 80*25*2);
	cursor.x = 0;
	cursor.y = 0;
}

void scroll(void) {
	if (cursor.y < 25)
		return;

	uint16 *vram16 = (uint16 *)videoram;

	for (int i = 0; i < 24*80; i++) {
		vram16[i] = vram16[i+80];
	}

	// Blank the last line
	memset(videoram + 80*24*2, 0, 80*2);

	// Move the cursor
	cursor.y = 24;
//	update_cursor();
}

int putchar(int c) {
	const unsigned int offset = cursor.y*80*2 + cursor.x*2;

	if (c == '\n') {
		// c == newline
		cursor.x = 0;
		cursor.y++;
	}
	else if (c == 0x08) {
		// Backspace
		if (cursor.x > 0)
			cursor.x--;
		// FIXME: doesn't move back across lines
	}
	else if (c >= 0x20) {
		// 0x20 is the lowest printable character (space)
		// Write the character
		videoram[offset] = (unsigned char)c;
		videoram[offset+1] = 0x07;

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
	outb(0x3d4, 14);
	outb(0x3d5, high);
	outb(0x3d4, 15);
	outb(0x3d5, low);

	// Impraper, software version:
//	videoram[cursor.y*80*2 + cursor.x*2] = 178;
//	videoram[cursor.y*80*2 + cursor.x*2 + 1] = 0x7;
}

void print(const char *str) {
	size_t len = strlen(str);

	for (size_t i = 0; i < len; i++) {
		putchar(str[i]);
	}
}

