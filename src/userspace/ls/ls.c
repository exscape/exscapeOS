#include <dirent.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <errno.h>

void print_help(void) {
	fprintf(stderr, "Usage: ls [-alh] [files...]\n");
	fprintf(stderr, "-1: display one file per line, with no additional details\n");
	fprintf(stderr, "-a: show hidden files (that begin with a dot)\n");
	fprintf(stderr, "-i: print each file's inode number (implies -l)\n");
	fprintf(stderr, "-l: show details, one file per line\n");
	fprintf(stderr, "-F: show a / after directory names\n");
	fprintf(stderr, "-h: display this help message\n");
}

// TODO: reduce amount of globals
int opt_all = 0, opt_list = 0, opt_singlecol = 0, opt_type = 0, opt_inode = 0;
int current_year = 0;
int line_used = 0; // used for standard mode only

int do_file(const char *fullname, const char *name) {
	struct stat st;
	if (stat(fullname, &st) != 0) {
		fprintf(stderr, "ls: ");
		perror(name);
		return 1;
	}

	if (opt_singlecol) {
		printf("%s%s\n", name, (opt_type && S_ISDIR(st.st_mode)) ? "/" : "");
		return 0;
	}
	else if (opt_list) {

		char perm_str[11] = "-rwxrwxrwx";
		if (S_ISDIR(st.st_mode))
			perm_str[0] = 'd';
		else if (S_ISCHR(st.st_mode))
			perm_str[0] = 'c';
		else if (S_ISBLK(st.st_mode))
			perm_str[0] = 'b';
		else if (S_ISLNK(st.st_mode))
			perm_str[0] = 'l';
		else if (S_ISFIFO(st.st_mode))
			perm_str[0] = 'p';
		else if (S_ISSOCK(st.st_mode))
			perm_str[0] = 's';
		else if ( ! S_ISREG(st.st_mode) ) {
			fprintf(stderr, "ls: warning: unknown permission for file %s\n", name);
		}

		for (int i=0; i<9; i++) {
			if (!(st.st_mode & (1 << i))) {
				perm_str[9 - i] = '-';
			}
		}

		time_t tmp = st.st_mtime;
		struct tm *tm = localtime(&tmp);
		char date_buf[16] = {0};
		if (tm->tm_year + 1900 == current_year)
			strftime(date_buf,  16, "%d %b %H:%M", tm);
		else
			strftime(date_buf, 16, "%d %b  %Y", tm);

		if (opt_inode) {
			printf("%7u ", (unsigned int)st.st_ino);
		}
		printf("%s %2d root  root %8u %s %s%s\n", perm_str, st.st_nlink, (uint32)st.st_size, date_buf, name, (opt_type && S_ISDIR(st.st_mode)) ? "/" : "");
	}
	else {
		// standard format
		if (line_used + strlen(name) + 4 > 80) {
			printf("\n");
			line_used = 0;
		}
		line_used += printf("%s%s    ", name, (opt_type && S_ISDIR(st.st_mode)) ? "/" : "");
	}

	return 0;
}

int do_dir(const char *dirname) {
	DIR *dir = opendir(dirname);
	if (!dir) {
		fprintf(stderr, "ls: ");
		perror(dirname);
		return 1;
	}

	struct dirent *dent;
	while ((dent = readdir(dir)) != NULL) {
		errno = 0;
		if (!opt_all && *(dent->d_name) == '.')
			continue;

		char name[1024] = {0};
		strlcpy(name, dirname, 1024);
		if (name[strlen(name) - 1] != '/')
			strlcat(name, "/", 1024);
		strlcat(name, dent->d_name, 1024);

		do_file(name, dent->d_name);
	}

	return 0;
}


int main(int argc, char **argv) {
	assert(argv[argc] == NULL);

	int c;
	while ((c = getopt(argc, argv, "1aFhli")) != -1) {
		switch(c) {
			case 'a':
				opt_all = 1;
				break;
			case 'l':
				opt_list = 1;
				opt_singlecol = 0;
				break;
			case 'i':
				opt_inode = 1;
				opt_list = 1;
				opt_singlecol = 0;
				break;
			case '1': /* overrides -l, if used later on in the command line */
				opt_singlecol = 1;
				opt_list = 0;
				break;
			case 'F':
				opt_type = 1;
				break;
			case 'h':
				// TODO: shouldn't be for help; 'h' is used for human-readable sizes in other ls implementations
				print_help();
				exit(0);
				break;
			default:
				exit(1);
				break;
		}
	}
	argc -= optind;
	argv += optind;

	const char **files = malloc((argc + 2) * sizeof(char *));
	memset(files, 0, (argc + 2) * sizeof(char *));

	if (argc == 0) {
		// no files/directories were passed as arguments
		files[0] = ".";
	}
	else
		memcpy(files, argv, argc * sizeof(char *));

	time_t tmp = time(NULL);
	struct tm *tm = localtime(&tmp);
	current_year = tm->tm_year + 1900;

	do {
		struct stat st;
		if (stat(*files, &st) != 0) {
			fprintf(stderr, "ls: ");
			perror(*files);
			continue;
		}

		if (argc > 1 && S_ISDIR(st.st_mode)) {
			printf("%s:\n", *files);
		}

		if (S_ISDIR(st.st_mode)) {
			do_dir(*files);
		}
		else
			do_file(*files, *files);

		if (errno) {
			fprintf(stderr, "ls: %s: %s\n", *files, strerror(errno));
		}

		if (argc > 1 && S_ISDIR(st.st_mode)) {
			printf("\n");
		}
	} while (*(++files));

	if (!opt_list && !opt_singlecol)
		printf("\n");
	else
		fflush(stdout);

	return 0;
}
