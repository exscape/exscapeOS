#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdbool.h>

#ifndef O_RDONLY
#define O_RDONLY 0
#endif

int main(int argc, char **argv) {
#define BUFSIZE (16*1024)
	char *buf = malloc(BUFSIZE);

	bool error = false;

	for (int i=1; i < argc || i == 1; i++) {
		const char *filename;
		int fd;

		if (argc == 1 || (argc > 1 && !strcmp(argv[i], "-"))) {
			filename = "(stdin)";
			fd = fileno(stdin);
		}
		else {
			filename = argv[i];
			fd = open(filename, O_RDONLY);
			if (fd < 0) {
				fprintf(stderr, "%s: %s: %s\n", argv[0], filename, strerror(errno));
				error = true;
				continue;
			}
			struct stat st;
			if (fstat(fd, &st) != 0) {
				fprintf(stderr, "cat: %s: %s\n", filename, strerror(errno));
				error = true;
				continue;
			}
			if (S_ISDIR(st.st_mode)) {
				fprintf(stderr, "cat: %s: %s\n", filename, strerror(EISDIR));
				error = true;
				continue;
			}
		}

		int r = 0;
		uint32 tot = 0;
		do {
			memset(buf, 0, BUFSIZE);
			r = read(fd, buf, BUFSIZE);
			if (r == -1) {
				fprintf(stderr, "%s: %s: %s\n", argv[0], filename, strerror(errno));
				break;
			}
			tot += r;
			if (r > 0)
				write(fileno(stdout), buf, r);
		} while (r > 0);

		if (!strcmp(filename, "stdin") != 0 && fd != fileno(stdin))
			close(fd);
	}

	if (!error)
		return 0;
	else
		return 1;
}
