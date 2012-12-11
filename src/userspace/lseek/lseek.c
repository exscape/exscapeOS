#include <exscapeos.h>
#define PATH "/initrd/initrd_test.txt"

// These will be in libc eventually
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define O_RDONLY 0

/*
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#define PATH "../../../misc/initrd_contents/initrd_test.txt"
*/

size_t strlen(const char *s) {
	size_t len = 0;
	while (*s++) len++;
	return len;
}

int main(int argc, char **argv) {
	int fd = open(PATH, O_RDONLY);
	if (fd < 0)
		return -1;

	// Ugly, but it works. Tested by verifying
	// that the same stuff is read into buf under
	// exscapeOS and OS X/Linux.

	char buf[128] = {0};
	read(fd, buf, 10);
	lseek(fd, 10, SEEK_SET);
	read(fd, buf + 10, 6);
	lseek(fd, -6, SEEK_CUR);
	read(fd, buf + 16, 4);
	lseek(fd, -3, SEEK_END);
	read(fd, buf + 20, 3);

	lseek(fd, -20, SEEK_CUR);
	read(fd, buf + 23, 7);
	lseek(fd, 0, SEEK_SET);
	read(fd, buf + 30, 5);
	lseek(fd, -11, SEEK_END);
	read(fd, buf + 35, 6);
	lseek(fd, -8, SEEK_CUR);
	read(fd, buf + 41, 5);

	// Make the whitespace more readable
	for (size_t i=0; i < strlen(buf); i++) {
		if (buf[i] == ' ')
			buf[i] = '_';
		else if(buf[i] == '\n')
			buf[i] = '\\';
		else if(buf[i] < ' ')
			buf[i] = '?';
	}

	write(1, buf, strlen(buf));

	return 0;
}
