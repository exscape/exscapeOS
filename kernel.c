typedef struct Point {
	unsigned int x, y;
} Point;


typedef unsigned long size_t;

void *memset(void *addr, int c, size_t n) {
	unsigned char *p = addr;
	for (size_t i = 0; i < n; i++) {
		*p++ = (unsigned char)c;
	}

	return addr;
}

int strlen(const char *str) {
	int len = 0;
	while (*str++ != 0) {
		len++;
	}

	return len;
}

void clrscr(void) {
   unsigned char *videoram = (unsigned char *) 0xb8000;

/*	int i = 0;
	for (i = 0; i < 80*25 * 2; i++) {
		videoram[i] = 0;
	} */
	memset(videoram, 0, 80*25*2);
}

void print(const Point *position, const char *str) {
   unsigned char *videoram = (unsigned char *) 0xb8000;

	int len = strlen(str);

	int offset = position->y*80*2 + position->x*2;

	for (int i = 0; i < len; i++) {
		videoram[2*i +   offset] = str[i];
		videoram[2*i+1 + offset] = 0x07;
	}
}

void panic(const char *str) {
   unsigned char *videoram = (unsigned char *) 0xb8000;

	// Clear screen
   clrscr();

	int len = strlen(str);
	for (int i = 0; i < len; i++) {
		videoram[2*i] = str[i];
		videoram[2*i+1] = 0x07;
	}

	// FIXME: halt, somehow
}
void kmain( void* mbd, unsigned int magic )
{
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
 
   Point cursor;
   cursor.x = 2;
   cursor.y = 1;

   clrscr();
   print(&cursor, "Hello, world! print()!");
}
