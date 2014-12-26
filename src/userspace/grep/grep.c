#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>

void print_usage(char **argv) {
	fprintf(stderr, "Usage: %s [options] <string-to-search-for> [filename(s)]\n", argv[0]);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -i for case insensitive search\n");
	fprintf(stderr, "  -c to print number of matches only\n");
	fprintf(stderr, "  -v to invert matching (print/count non-matching lines)\n");
	fprintf(stderr, "Leave filename empty to search stdin, or do grep <str> -- -.\n");
	fprintf(stderr, "Examples:\n");
	fprintf(stderr, "%s string file1 file2\n", argv[0]);
	fprintf(stderr, "cat file | %s string\n", argv[0]);
	fprintf(stderr, "cat file1 | %s string - file2\n", argv[0]);
}

int opt_case = 0, opt_count = 0, opt_invert = 0;

int main(int argc, char **argv) {
	bool error = false;
	bool match = false;
	int c;
	while ((c = getopt(argc, argv, "cihv")) != -1) {
		switch (c) {
			case 'h':
				print_usage(argv);
				exit(0);
				break;
			case 'i':
				opt_case = 1;
				break;
			case 'c':
				opt_count = 1;
				break;
			case 'v':
				opt_invert = 1;
				break;
			case 0:
				fprintf(stderr, "getopt returned 0; if you used - for stdin, try -- - instead\n");
				exit(1);
				break;
			default:
				printf("grep: unknown option %c\n", c);
				exit(1);
				break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		print_usage(argv - optind);
		exit(1);
	}

	char *needle = argv[0];
	argv++;
	argc--;

	for (int i = 0; i < argc || i == 0; i++) {
		const char *filename = NULL;
		int fd;

		if ((argc == 0 && i == 0) || (argc > i && !strcmp(argv[i], "-"))) {
			// Use stdin
			filename = "(stdin)";
			fd = fileno(stdin);
		}
		else {
			filename = argv[i];
			fd = open(filename, O_RDONLY);
			if (fd < 0) {
				fprintf(stderr, "grep: %s: %s\n", filename, strerror(errno));
				error = true;
				continue;
			}
			struct stat st;
			if (fstat(fd, &st) != 0) {
				fprintf(stderr, "grep: %s: %s\n", filename, strerror(errno));
				error = true;
				continue;
			}
			if (S_ISDIR(st.st_mode)) {
				fprintf(stderr, "grep: %s: %s\n", filename, strerror(EISDIR));
				error = true;
				continue;
			}
		}

		char *p;
		char line[4096] = {0};
		int matches = 0; // matches for this file only
		FILE *f = fdopen(fd, "r");
		if (!f) {
			fprintf(stderr, "grep: fdopen: %s\n", strerror(errno));
			error = true;
			continue;
		}

		while ((p = fgets(line, 4096, f)) != NULL) {
			bool result = opt_case ? strcasestr(line, needle) : strstr(line, needle);
			if (opt_invert)
				result = !result;

			if (result) {
				match = true;
				matches++;
				if (!opt_count) {
					if (argc > 1)
						printf("%s:", filename);
						printf("%s", line);
					fflush(stdout);
				}
			}
		}
		if (opt_count) {
			if (argc > 1)
				printf("%s:", filename);
			printf("%d\n", matches);
		}
	}

	if (error)
		return 2;
	else
		return (match) ? 0 : 1;
}
