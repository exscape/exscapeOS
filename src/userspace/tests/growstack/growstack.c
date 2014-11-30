#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdbool.h>

static int MAX_ALLOC = 0;
void recursive_stack(int i) {
#define BUFSIZE 512
	if (i > MAX_ALLOC/BUFSIZE)
		return;

	char buf[BUFSIZE];
	memset(buf, 0, BUFSIZE);
	printf("in recursive_stack #%d, approx. %d bytes used total\n", i, BUFSIZE * i);
	recursive_stack(i + 1);

}

int main(int argc, char **argv) {
	if (argc != 2 || (MAX_ALLOC = 1024 * atoi(argv[1])) == 0) {
		printf("Usage: %s <approximate number of kiB to allocate on userspace stack>\n", argv[0]);
	}
	recursive_stack(1);

	return 0;
}
