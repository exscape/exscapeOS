#include <stdio.h>
#include <stdlib.h>

// Simple program to test on-demand (on #PF) stack growing

int main(int argc, char **argv) {
	unsigned int max;
	if (argc != 2 || (max = 1024*atoi(argv[1])) == 0) {
		fprintf(stderr, "Usage: %s <number of kiB to allocate>\n", argv[0]);
		return 1;
	}

	char stack;
	char *p = &stack - 4096; /* move away from the part of the stack that's populated */

	unsigned int sum = 0; /* should remain 0, or memory wasn't zeroed properly */
	while (p >= &stack - max) { sum += *p--; }
	printf("Done, %d kbytes allocated/read\n", max/1024);
	printf("Sum = %u\n", sum);

	return 0;
}
