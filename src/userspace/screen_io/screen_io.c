#include <exscapeos.h>

int main(int argc, char **argv) {
	const char *s = "Hello world!\n";
	write(1, s, 13); // We have no strlen()/libc yet!

	return 0;
}
