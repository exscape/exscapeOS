#include <exscapeos.h>

int main() {
	char *p = malloc(512);
	strcpy(p, "Hello, world! This string is read from usermode heap memory.");
	puts(p);

	return 0;
}
