#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>

#ifndef O_RDONLY
#define O_RDONLY 0
#endif

int main(int argc, char **argv) {
	if (argc == 1)
		return 0;

#define BUFSIZE (16*1024)
	char *buf = malloc(BUFSIZE);

	bool error = false;

	for (int i=1; i < argc; i++) {
		int fd = open(argv[i], O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
			error = true;
			continue;
		}

		int r = 0;
		uint32 tot = 0;
		do {
			memset(buf, 0, BUFSIZE);
			r = read(fd, buf, BUFSIZE);
			if (r == -1) {
				fprintf(stderr, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
				break;
			}
			tot += r;
			if (r > 0)
				write(fileno(stdout), buf, r);
		} while (r > 0);

		close(fd);
	}

	if (!error)
		return 0;
	else
		return 1;
}
