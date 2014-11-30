#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>

void print_usage(char **argv) {
	fprintf(stderr, "Usage: %s string-to-search-for\n", argv[0]);
}

int main(int argc, char **argv) {
	if (argc > 2) {
		fprintf(stderr, "grep: input only supported from stdin so far.\n");
		print_usage(argv);
		exit(1);
	}
	else if (argc != 2) {
		print_usage(argv);
		exit(1);
	}

	char *p;
	char line[4096] = {0};
	bool match = false;
	while ((p = fgets(line, 4096, stdin)) != NULL) {
		if (strstr(line, argv[1])) {
			printf("%s", line);
			fflush(stdout);
			match = true;
		}
	}

	return (match == true) ? 0 : 1;
}
