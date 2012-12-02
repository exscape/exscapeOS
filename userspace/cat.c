#include <exscapeos.h>

#define O_RDONLY 0

int main(int argc, char **argv) {
	char *path = "/test.txt"; // TODO

	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		puts("Error: fd < 0");
		return 0;
	}

	char buf[512] = {0};

	int r = 0;
	do {
		// TODO: memset
		for (int i=0; i < 512; i++)
			buf[i] = 0;

		r = read(fd, buf, 511);
		puts(buf);
	} while (r > 0);

	close(fd);

	return 0;
}
