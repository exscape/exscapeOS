#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
//#define PATH "/Volumes/EXSCAPEOS 1/test.txt"
#define PATH "/test.txt"

int main(int argc, char **argv) {
	int fd = open(PATH, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Unable to open file!\n");
		return -1;
	}

	// Ugly, but it works. Tested by verifying
	// that the same stuff is read into buf under
	// exscapeOS and OS X/Linux.

	char buf[128] = {0};
	read(fd, buf, 10);
	lseek(fd, 20, SEEK_SET);
	read(fd, buf + 10, 6);
	lseek(fd, -6, SEEK_CUR);
	read(fd, buf + 16, 4);
	lseek(fd, -7, SEEK_END);
	read(fd, buf + 20, 3);

	lseek(fd, -30, SEEK_CUR);
	read(fd, buf + 23, 7);
	lseek(fd, 12000, SEEK_SET);
	read(fd, buf + 30, 5);
	lseek(fd, -18, SEEK_END);
	read(fd, buf + 35, 6);
	lseek(fd, -4500, SEEK_CUR);
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

	write(1, "\nOne: ", 6);
	memset(buf, 0, 128);
	read(0, buf, 128);

	char buf2[128] = {0};
	write(1, "Two: ", 5);
	memset(buf2, 0, 128);
	read(0, buf2, 128);

	write(1, "You entered: ", 13);
	write(1, buf, strlen(buf) - 1); // strip \n
	write(1, " ", 1);
	write(1, buf2, strlen(buf2)); // don't strip

	return 0;
}
