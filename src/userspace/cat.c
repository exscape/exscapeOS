#include <exscapeos.h>

#define O_RDONLY 0

int main(int argc, char **argv) {
	if (argc == 1)
		return 0;

	char buf[512] = {0};

	for (int i=1; i < argc; i++) {
		if (argv[i][0] != '/') {
			buf[0] = '/';
			for (int j=0;; j++) {
				if (argv[i][j] != 0)
					buf[1+j] = argv[i][j];
				else {
					buf[1+j] = 0;
					break;
				}
			}
		}

		int fd = open(buf, O_RDONLY);
		if (fd < 0) {
			puts("error opening ");
			puts(argv[i]);
			puts(", exiting");
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
