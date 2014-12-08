#include <dirent.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <errno.h>
#include <stdbool.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#define SIGIL(m) (opt_sigil ? \
				 (S_ISLNK(m) ? "@" : \
				 (S_ISDIR(m) ? "/" : \
				 ((m & (S_IXUSR | S_IXGRP | S_IXOTH)) ? "*" : ""))) : "")

void print_help(void) {
	fprintf(stderr, "Usage: ls [-alh] [files...]\n");
	fprintf(stderr, "-1: display one file per line, with no additional details\n");
	fprintf(stderr, "-a: show hidden files (that begin with a dot)\n");
	fprintf(stderr, "-i: print each file's inode number (implies -l)\n");
	fprintf(stderr, "-l: show details, one file per line\n");
	fprintf(stderr, "-R: recurse into all subdirectories\n");
	fprintf(stderr, "-F: show sigils, e.g. / for dirs, @ for symlinks\n");
	fprintf(stderr, "-h: display this help message\n");
}

// TODO: add support for human-readable sizes (-H? or use getopt_long and support --help? the help argument needs to remain as either -h or --help!)

int opt_all = 0, opt_list = 0, opt_singlecol = 0, opt_sigil = 0, opt_inode = 0, opt_recurse = 0;
int current_year = 0;
const unsigned int screen_width = 80;
bool first = true;

int do_file(const char *fullname, const char *name, struct stat *st) {
	if (opt_singlecol) {
		printf("%s%s\n", name, SIGIL(st->st_mode));
		return 0;
	}
	else if (opt_list) {
		char perm_str[11] = "-rwxrwxrwx";
		if (S_ISDIR(st->st_mode))
		perm_str[0] = 'd';
		else if (S_ISCHR(st->st_mode))
			perm_str[0] = 'c';
		else if (S_ISBLK(st->st_mode))
			perm_str[0] = 'b';
		else if (S_ISLNK(st->st_mode))
			perm_str[0] = 'l';
		else if (S_ISFIFO(st->st_mode))
			perm_str[0] = 'p';
		else if (S_ISSOCK(st->st_mode))
			perm_str[0] = 's';
		else if ( ! S_ISREG(st->st_mode) ) {
			fprintf(stderr, "ls: warning: unknown permission for file %s\n", name);
		}

		for (int i=0; i<9; i++) {
			if (!(st->st_mode & (1 << i))) {
				perm_str[9 - i] = '-';
			}
		}

		time_t tmp = st->st_mtime;
		struct tm *tm = localtime(&tmp);
		char date_buf[16] = {0};
		if (tm->tm_year + 1900 == current_year)
			strftime(date_buf, 16, "%d %b %H:%M", tm);
		else
			strftime(date_buf, 16, "%d %b  %Y", tm);

		if (opt_inode) {
			printf("%7u ", (unsigned int)st->st_ino);
		}
		printf("%s %2d root  root %8u %s %s%s", perm_str, st->st_nlink, (unsigned int)st->st_size, date_buf, name, SIGIL(st->st_mode));

		if (S_ISLNK(st->st_mode)) {
			char link_target[PATH_MAX+1] = {0};
			readlink(fullname, link_target, PATH_MAX);
			// TODO: print sigil for link target?
			printf(" -> %s", link_target);
		}

		printf("\n");
	}
	else {
		// standard format
		printf("%s%s", name, SIGIL(st->st_mode));
	}

	return 0;
}

static int dir_alphasort (const struct dirent **a, const struct dirent **b) {
	// Compare two dirents and order them
	return strcmp((*a)->d_name, (*b)->d_name);
}

int do_dir(const char *dirname) {
	struct dirent **entries;
	struct stat **st_entries;
	int *errnos;
	int n; // number of directory entries total
	int num_entries = 0; // # of entries to print; smaller than n unless the -a option is used
	size_t longest = 0;
	char relpath[PATH_MAX+1] = {0};

	if (opt_recurse) {
		if (!first) printf("\n");
		printf("%s:\n", dirname);
	}

	first = false;

	if ((n = scandir(dirname, &entries, NULL, dir_alphasort)) < 0) {
		fprintf(stderr, "\nls: ");
		perror(dirname);
		return 1;
	}

	st_entries = malloc(n * sizeof(struct stat *));
	errnos = malloc(n * sizeof(errno));
	for (int i = 0; i < n; i++) {
		// Keep track of the longest name; used for the "normal" mode (not -1 and not -l)
		if (strlen(entries[i]->d_name) > longest)
			longest = strlen(entries[i]->d_name);

		// Count the number of entries we're going to print
		if (opt_all || entries[i]->d_name[0] != '.')
			num_entries++;

		// stat everything; scandir copies the results from getdents and doesn't set d_type,
		// so we need to stat to find out whether something is a dir/link to dir or a file
		st_entries[i] = malloc(sizeof(struct stat));
		snprintf(relpath, PATH_MAX, "%s/%s", dirname, entries[i]->d_name);
		if (stat(relpath, st_entries[i]) != 0) {
			errnos[i] = errno;
		}
		else
			errnos[i] = 0;
	}

	if (opt_sigil)
		longest++;

	//
	// First, do this directory. Print everything out in a nice layout.
	//
	if (opt_singlecol || opt_list) {
		// Ahh, easy.
		for (int i = 0; i < n; i++) {
			if (!opt_all && entries[i]->d_name[0] == '.')
				continue;
			snprintf(relpath, PATH_MAX, "%s/%s", dirname, entries[i]->d_name);
			struct stat linkst;
			if (lstat(relpath, &linkst) == 0)
				do_file(relpath, entries[i]->d_name, &linkst);
			else
				perror(relpath);
		}
	}
	else {
		// Use the normal layout, with columns etc.
		// After thinking and fiddling with test data for quite a while, I couldn't
		// figure out a solution that was both relatively simple in logic,
		// and created pretty results. For example, if you try to ls 7 items in
		// GNU coreutils, it can print them as 7 on one line, 4+3, 3+2+2, 2+2+2+1,
		// and of course 7 lines with 1 on each.
		// Instead of doing the math, I figured I could simply store it in a 2D array,
		// as it should be displayed on screen, and go from there. The results
		// aren't as good, but it does work.

		int cols = screen_width/(longest + 1);
		if (cols == 0)
			cols = 1;
		int rows = num_entries/cols;
		if (num_entries % cols)
			rows++;

		// Directories containing just 1 long file look awful without this cap;
		// say the screen is 80 chars (as always in exscapeOS as of this writing),
		// and a file is 85 chars. printf will pad ALL files to 85 chars, so each file,
		// even ones that are just 3-4 chars long, will still use two lines.
		if (longest > screen_width)
			longest = screen_width;

		struct dirent **grid = malloc(sizeof(struct dirent *) * cols * rows);
		for (int i=0; i<cols*rows;i++) grid[i] = NULL;

		// Fill the grid; note that this is done "backwards" (the outer loop would
		// typically be rows), to transpose it. So we first fill the the first column,
		// then the second, etc, going down, then back up and right one step, etc.
		for (int col = 0, i = 0; col < cols; col++) {
			for (int row = 0; row < rows; row++) {
				if (i < n) {
					while (!opt_all && i < n && entries[i]->d_name[0] == '.') {
						// Skip dotfiles
						i++;
					}
					grid[col + row*cols] = entries[i++];
				}
			}
		}

		// Print it "the right way"; this way, we've sort of transposed it
		for (int row = 0; row < rows; row++) {
			for (int col = 0; col < cols; col++) {
				int ind = row*cols + col;
				if (grid[ind] != NULL) {
					char name[1024] = {0};
					size_t j = 0;
					for (size_t i = 0; i < strlen(grid[ind]->d_name); i++) {
						if (grid[ind]->d_name[i] >= ' ')
							name[j++] = grid[ind]->d_name[i];
					}
					name[j] = 0;

					printf("%-*s", (int)longest, name);
				}
				//else
				//printf("what do?");
				if (col + 1 < cols)
					printf(" ");
			}
			printf("\n");
		}
	}

	// Next, recurse into the directories, in order
	if (opt_recurse) {
		for (int i = 0; i < n; i++) {
			if (!opt_all && entries[i]->d_name[0] == '.')
				continue;
			if (strcmp(entries[i]->d_name, ".") == 0 || strcmp(entries[i]->d_name, "..") == 0)
				continue;
			if (S_ISDIR(st_entries[i]->st_mode)) {
				snprintf (relpath, PATH_MAX, "%s/%s", dirname, entries[i]->d_name);
				do_dir(relpath);
			}
		}
	}

	// Free everything we've allocated
	for (int i = 0; i < n; i++) {
		free(entries[i]);
		free(st_entries[i]);
	}
	free(entries);
	free(st_entries);

	return 0;
}

int main(int argc, char **argv) {
	assert(argv[argc] == NULL);

	int c;
	while ((c = getopt(argc, argv, "1aFhlRi")) != -1) {
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
			case 'R':
				opt_recurse = 1;
				break;
			case 'F':
				opt_sigil = 1;
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

	int num_processed = 0;
	do {
		struct stat st;
		if (lstat(*files, &st) != 0) {
			fprintf(stderr, "ls: ");
			perror(*files);
			num_processed++;
			continue;
		}

		if (argc > 1 && S_ISDIR(st.st_mode) && !opt_recurse) {
			// if opt_recurse is set, we'd print this twice
			printf("%s:\n", *files);
		}

		// TODO: sort arguments so that all files are printed
		// prior to directories

		if (S_ISDIR(st.st_mode)) {
			do_dir(*files);
		}
		else if (S_ISLNK(st.st_mode)) {
			struct stat linkst;
			if (stat(*files, &linkst) != 0) {
				fprintf(stderr, "ls: ");
				perror(*files);
				continue;
			}

			if (S_ISDIR(linkst.st_mode))
				do_dir(*files);
		}
		else
			do_file(*files, *files, &st);

		if (argc > num_processed + 1 && S_ISDIR(st.st_mode) && !opt_recurse) {
			printf("\n");
		}

		num_processed++;
	} while (*(++files));

	fflush(stdout);

	return 0;
}
