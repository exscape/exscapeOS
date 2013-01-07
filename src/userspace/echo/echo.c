#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>

int main(int argc, char **argv) {
	bool newline = true;

	argc--;
	argv++;

	if (argc >= 1 && strcmp(argv[0], "-n") == 0) {
		newline = false;
		argc--;
		argv++;
	}

	for (int i = 0; i < argc; i++) {
		printf("%s", argv[i]);
		if (i < argc - 1)
			printf(" ");
	}

	if (newline)
		printf("\n");

	return 0;
}
