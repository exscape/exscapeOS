#include <exscapeos.h>

#define O_RDONLY 0

int main(int argc, char **argv) {
	if (argc == 1)
		return 0;

	char buf[512];

	for (int i=1; i < argc; i++) {
		int fd = open(argv[i], O_RDONLY);
		if (fd < 0) {
			puts("error opening ");
			puts(argv[i]);
			puts(", exiting\n");
			return 0;
		}

		int r = 0;
		do {
			// TODO: memset
			for (int j=0; j < 512; j++)
				buf[j] = 0;

			r = read(fd, buf, 511);
			puts(buf);
		} while (r > 0);

		close(fd);
	}

	return 0;
}
