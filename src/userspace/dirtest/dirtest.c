#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char **argv) {
	int fd = open("/OLD", 0);

	printf("fd from open = %d\n", fd);

	char buf[128] = {0};

	int r = read(fd, buf, 128);

	printf("read returned %d\n", r);
	if (r == -1) {
		printf("  errno = %d (%s)\n", errno, strerror(errno));
	}

	return 0;
}
