#include <types.h>

// TODO: Proper makefile for .s -> .o and linking

static unsigned char *videoram = (unsigned char *) 0xb8000;
static Point cursor;

typedef struct Time {
	unsigned short year;
	unsigned char month;
	unsigned char day;
	unsigned char hour;
	unsigned char minute;
	unsigned char second;
} Time;

int putchar(int c);
void *memset(void *addr, int c, size_t n);
size_t strlen(const char *str);
void panic(const char *str);
void print(const char *str);
void clrscr(void);
void kmain(void* mbd, unsigned int magic);
void get_time(Time *);

void *memset(void *addr, int c, size_t n) {
	unsigned char *p = addr;

	for (size_t i = 0; i < n; i++) {
		*p++ = (unsigned char)c;
	}

	return addr;
}

size_t strlen(const char *str) {
	size_t len = 0;

	while (*str++ != 0) {
		len++;
	}

	return len;
}

void clrscr(void) {
	memset(videoram, 0, 80*25*2);
	cursor.x = 0;
	cursor.y = 0;
}

int putchar(int c) {
	const unsigned int offset = cursor.y*80*2 + cursor.x*2;

	if (c != '\n') {
		// Write the character
		videoram[offset] = (unsigned char)c; // FIXME: does this work as expected?
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
	else {
		// c == newline
		cursor.x = 0;
		cursor.y++;
	}

	// Move the cursor
	videoram[cursor.y*80*2 + cursor.x*2] = 178;
	videoram[cursor.y*80*2 + cursor.x*2 + 1] = 0x7;

	return c;
}

void print(const char *str) {
	size_t len = strlen(str);

	for (size_t i = 0; i < len; i++) {
		putchar(str[i]);
	}
}

void panic(const char *str) {
	clrscr();
	print("PANIC: ");
	print(str);
	asm("hangloop: hlt ; jmp hangloop");
	// FIXME: halt, somehow
}

void get_time(Time *t) {
	/* This function isn't pretty, at all, but it didn't appear very easy to share
	   structs between nasm and C... So I chose inline assembly. */
	unsigned char yeartmp = 0;

//	memset(t, 0, sizeof(Time));

	asm(
		/* make sure the update flag isn't set */
		".ll:"
		"movb $10, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"testb $0x80, %%al;"
		"jne .ll;"

		/* fetch the year (00-99) */
		"movb $0x09, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[yeartmp];"

		/* month */
		"movb $0x08, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[month];"

		/* day of month */
		"movb $0x07, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[day];"

		/* hour (12h or 24h) */
		"movb $0x04, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[hour];"

		/* minute */
		"movb $0x02, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[minute];"

		/* second */
		"movb $0x0, %%al;"
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[second];"

		: 
		[yeartmp]"=m"(yeartmp),
		[month]"=m"(t->month),
		[day]  "=m"(t->day),
		[hour] "=m"(t->hour),
		[minute]"=m"(t->minute),
		[second]"=m"(t->second)
		: : "%al", "memory");


	unsigned char regb;
	asm("movb $0xb, %%al;" // status reg B
		"outb %%al, $0x70;"
		"inb $0x71, %%al;"
		"movb %%al, %[regb];"
		:
		[regb]"=m"(regb)
		: : "%al", "memory");

	unsigned char is_bcd, is_24_hour;
	is_bcd     = (regb & 4) ? 0 : 1; /* [sic] */
	is_24_hour = (regb & 2) ? 1 : 0; 

	if (is_bcd) {
#define bcd_to_bin(x) ((x / 16) * 10) + (unsigned char)(x & (unsigned char)0xf)
		// Convert from BCD to decimal form
		yeartmp   = bcd_to_bin(yeartmp);
		t->month  = bcd_to_bin(t->month);
		t->day    = bcd_to_bin(t->day);
		t->hour   = bcd_to_bin(t->hour);
		t->minute = bcd_to_bin(t->minute);
		t->second = bcd_to_bin(t->second);
	}

	if (!is_24_hour) {
		// TODO: Get PM flag and adjust t->hour
	}

	t->year = 2000 + yeartmp; // TODO: get century from RTC (in case this lives on for 89+ years! ;)
}

void kmain(void* mbd, unsigned int magic) {
   if ( magic != 0x2BADB002 )
   {
      /* Something went not according to specs. Print an error */
      /* message and halt, but do *not* rely on the multiboot */
      /* data structure. */
   }
 
   mbd = mbd; // silence warning

   /* You could either use multiboot.h */
   /* (http://www.gnu.org/software/grub/manual/multiboot/multiboot.html#multiboot_002eh) */
   /* or do your offsets yourself. The following is merely an example. */ 
   //char * boot_loader_name =(char*) ((long*)mbd)[16];
   clrscr();

   // Initialize cursor
   cursor.x = 0;
   cursor.y = 0;

   print("Hello world! Printing works, and keeping track of the cursor as well! Blah blah");
   print("\nThis is on a new line.\n");
   print("This is also on a new line.\nThis too!");
   print("Different print, SAME line!\nNEW line.");

   Time t;
   memset(&t, 0, sizeof(t));
   get_time(&t);
}
