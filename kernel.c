typedef struct Point {
	unsigned int x, y;
} Point;

int strlen(const char *str) {
	int len = 0;
	const char *p = str;
	while (*p++ != 0) {
		len++;
	}

	return len;
}

void clrscr(void) {
   unsigned char *videoram = (unsigned char *) 0xb8000;

	int i = 0;
	for (i = 0; i < 80*25 * 2; i++) {
		videoram[i] = 0;
	}
}

void print(const Point *position, const char *str) {
   unsigned char *videoram = (unsigned char *) 0xb8000;

	int len = strlen(str);

	int i = 0;

	// FIXME: prints at the wrong coordinates

	for (i = 0; i < len; i++) {
		videoram[2*i +   2*(position->y*25 + position->x)] = str[i];
		videoram[2*i+1 + 2*(position->y*25 + position->x)] = 0x07;
	}
}

void panic(const char *str) {
   unsigned char *videoram = (unsigned char *) 0xb8000;

	int len = strlen(str);

	int i = 0;

	// Clear screen
	for (i = 0; i < 80*25 * 2; i++) {
		videoram[i] = 0;
	}

	for (i = 0; i < len; i++) {
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
 
   /* Print a letter to screen to see everything is working: */
//   videoram[0] = 65; /* character 'A' */
//   videoram[1] = 0x07; /* light grey (7) on black (0). */
/*
	char *greeting = "Hello world!";
	int greeting_len = strlen(greeting);

	int i = 0;

	// Clear screen
	for (i = 0; i < 80*25 * 2; i++) {
		videoram[i] = 0;
	}

	for (i = 0; i < greeting_len; i++) {
		videoram[2*i] = greeting[i];
		videoram[2*i+1] = 0x07;
	}
	*/

   Point cursor;
   cursor.x = 1;
   cursor.y = 2;

   clrscr();
   print(&cursor, "Hello, world! print()!");
}
