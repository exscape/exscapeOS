#include <exscapeos.h>

int main(int argc, char **argv) {
	for (char i=0; i < argc; i++) {
		puts("argv[");
		putchar(i + 0x30);
		puts("] = ");
		puts(argv[i]);
		putchar('\n');
	}
	return 0;
}
