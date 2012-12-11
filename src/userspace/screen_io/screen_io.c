#include <exscapeos.h>

size_t strlen(const char *s) {
	size_t len = 0;
	while (*s++) len++;
	return len;
}

int main(int argc, char **argv) {
	const char *s = "Hello world!\n", *s2 = "Input: ";
	const char *s3 = "You wrote: ";
	char buf[256] = {0};
	write(1, s, strlen(s));

	write(1, s2, strlen(s2));
	read(0, buf, sizeof(buf));

	write(1, s3, strlen(s3));
	write(1, buf, strlen(buf));

	return 0;
}
