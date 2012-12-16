#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#ifndef O_RDONLY
#define O_RDONLY 0
#endif

int main(int argc, char **argv) {
	if (argc == 1)
		return 0;

	char buf[512];

	bool error = false;

	for (int i=1; i < argc; i++) {
		int fd = open(argv[i], O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
			error = true;
		}

		int r = 0;
		do {
			memset(buf, 0, 512);
			r = read(fd, buf, 511);
			fputs(buf, stdout);
		} while (r > 0);

		close(fd);
	}

	if (!error)
		return 0;
	else
		return 1;
}
