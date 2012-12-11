#include <exscapeos.h>

int main(int argc, char **argv) {
	const char *s = "Hello world!\n", *s2 = "Input: ";
	const char *s3 = "You wrote: ";
	char buf[256] = {0};
	write(1, s, 13); // We have no strlen()/libc yet!

	write(1, s2, 7);
	read(0, buf, 255);

	int len = 0;
	char *p = buf;
	while (*p) {
		len++, p++;
	}

	write(1, s3, 11);
	write(1, buf, len);
	putchar('\n');

	return 0;
}
