#include <exscapeos.h>

int main(int argc, char **argv) {
	for (int i=0; i < argc; i++) {
		puts("argv[");
		putchar((char)(i + 0x30));
		puts("] = ");
		puts(argv[i]);
		putchar('\n');
	}
	return 0;
}
