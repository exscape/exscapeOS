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
	fprintf(stderr, "Usage: ls [-options] [files...]\n");
	fprintf(stderr, "-1: display one file per line, with no additional details\n");
	fprintf(stderr, "-a: show hidden files (files that begin with a dot)\n");
	fprintf(stderr, "-l: show details, one file per line\n");
	fprintf(stderr, "-i: print each file's inode number (implies -l)\n");
	fprintf(stderr, "-d: show directories instead of their contents\n");
	fprintf(stderr, "-R: recurse into all subdirectories\n");
	fprintf(stderr, "-F: show sigils, e.g. / for dirs, @ for symlinks\n");
	fprintf(stderr, "-h: display this help message\n");
}

int do_dir(const char *dirname);

// TODO: add support for human-readable sizes (-H? or use getopt_long and support --help? the help argument needs to remain as either -h or --help!)

int opt_all = 0, opt_list = 0, opt_singlecol = 0, opt_sigil = 0, opt_inode = 0, opt_recurse = 0, opt_show_dir = 0;
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

		for (int i = 0; i < 9; i++) {
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

void print_entries(const char **names, const char *dirname, int n) {
	struct stat **st_entries; // stat results for each entry
	int *errnos; // errnos for each stat call
	int num_entries = 0; // # of entries to print; smaller than n unless the -a option is used
	size_t longest = 0; // longest name in this directory
	char relpath[PATH_MAX+1] = {0}; // re-used as a temporary buffer for paths

	st_entries = malloc(n * sizeof(struct stat *));
	errnos = malloc(n * sizeof(errno));
	for (int i = 0; i < n; i++) {
		// Keep track of the longest name; used for the "normal" mode (not -1 and not -l)
		if (strlen(names[i]) > longest)
			longest = strlen(names[i]);

		// Count the number of names we're going to print
		if (opt_all || names[i][0] != '.')
			num_entries++;

		// stat everything; scandir copies the results from getdents and doesn't set d_type,
		// so we need to stat to find out whether something is a dir/link to dir or a file
		st_entries[i] = malloc(sizeof(struct stat));
		snprintf(relpath, PATH_MAX, "%s/%s", dirname, names[i]);
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
			if (!opt_all && names[i][0] == '.')
				continue;
			snprintf(relpath, PATH_MAX, "%s/%s", dirname, names[i]);
			struct stat linkst;
			if (lstat(relpath, &linkst) == 0)
				do_file(relpath, names[i], &linkst);
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

		const char **grid = malloc(sizeof(struct dirent *) * cols * rows);
		for (int i = 0; i < cols*rows; i++) grid[i] = NULL;

		// Fill the grid; note that this is done "backwards" (the outer loop would
		// typically be rows), to transpose it. So we first fill the the first column,
		// then the second, etc, going down, then back up and right one step, etc.
		for (int col = 0, i = 0; col < cols; col++) {
			for (int row = 0; row < rows; row++) {
				if (i < n) {
					while (!opt_all && i < n && names[i][0] == '.') {
						// Skip dotfiles
						i++;
					}
					grid[col + row*cols] = names[i++];
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
					for (size_t i = 0; i < strlen(grid[ind]); i++) {
						if (grid[ind][i] >= ' ')
							name[j++] = grid[ind][i];
					}
					name[j] = 0;

					printf("%-*s", (int)longest, name);
				}
				if (col + 1 < cols)
					printf(" ");
			}
			printf("\n");
		}

		free(grid);
	}

	// Next, recurse into the directories, in order
	if (opt_recurse) {
		for (int i = 0; i < n; i++) {
			if (!opt_all && names[i][0] == '.')
				continue;
			if (strcmp(names[i], ".") == 0 || strcmp(names[i], "..") == 0)
				continue;
			if (S_ISDIR(st_entries[i]->st_mode)) {
				snprintf(relpath, PATH_MAX, "%s/%s", dirname, names[i]);
				do_dir(relpath);
			}
		}
	}

	// Free everything we've allocated
	for (int i = 0; i < n; i++)
		free(st_entries[i]);

	free(st_entries);
	free(errnos);
}

int do_dir(const char *dirname) {
	struct dirent **d_entries;
	char **names; // d_name entries from the above
	int n; // number of directory entries total

	if (opt_recurse) {
		if (!first) printf("\n");
		printf("%s:\n", dirname);
	}

	first = false;

	if ((n = scandir(dirname, &d_entries, NULL, dir_alphasort)) < 0) {
		fprintf(stderr, "\nls: ");
		perror(dirname);
		return 1;
	}

	names = malloc(n * sizeof(const char *));
	for (int i = 0; i < n; i++) {
		names[i] = strdup(d_entries[i]->d_name);
		free(d_entries[i]);
	}
	free(d_entries);

	print_entries((const char **)names, dirname, n);

	// The d_entries array is freed above, so this is all we need to free now
	for (int i = 0; i < n; i++) {
		free(names[i]);
	}
	free(names);

	return 0;
}

int main(int argc, char **argv) {
	assert(argv[argc] == NULL);

	int c;
	while ((c = getopt(argc, argv, "1aFhlRid")) != -1) {
		switch(c) {
			case 'a':
				opt_all = 1;
				break;
			case '1':
				opt_singlecol = 1;
				opt_list = 0;
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
			case 'd':
				opt_show_dir = 1;
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

	const char **names = malloc((argc + 1) * sizeof(char *));
	memset(names, 0, (argc + 1) * sizeof(char *));

	if (argc == 0) {
		// no files/directories were passed as arguments
		names[0] = ".";
		argc = 1;
	}
	else
		memcpy(names, argv, argc * sizeof(char *));

	time_t tmp = time(NULL);
	struct tm *tm = localtime(&tmp);
	current_year = tm->tm_year + 1900;

	if (opt_show_dir) {
		// This option *really* simplifies things.
		if (argc == 1 && !strcmp(names[0], "."))
			opt_all = 1;
		print_entries(names, ".", argc);
		goto out_show_dir; // Better than nesting the following >50 lines
	}

	// First off, stat all arguments. That way,
	// we know which are files, and which are
	// directories (or links to directories).
	// Files should be printed first (unless -d is specified, see above).
	struct stat **st = calloc(argc, sizeof(struct stat *));
	int *errnos = calloc(argc, sizeof(int));

	for (int i = 0; i < argc; i++) {
		st[i] = malloc(sizeof(struct stat));
		if (stat(names[i], st[i]) != 0) {
			fprintf(stderr, "ls: ");
			perror(names[i]);
			errnos[i] = errno;
		}
		else
			errnos[i] = 0;
	}

	// Eh... we could count them first, then allocate, then sort, but that wastes time.
	// This wastes AT MOST a few kiB of RAM instead.
	const char **dirs = calloc(argc, sizeof(char *));
	const char **files = calloc(argc, sizeof(char *));

	int num_dirs = 0;
	int num_files = 0;
	for (int i = 0; i < argc; i++) {
		if (errnos[i] == 0) {
			if (S_ISDIR(st[i]->st_mode))
				dirs[num_dirs++] = names[i];
			else
				files[num_files++] = names[i];
		}
	}

	// Finally, start showing stuff!
	// First, all command line arguments that were actually files.
	if (num_files > 0) {
		print_entries(files, ".", num_files);
		if (num_dirs > 0)
			printf("\n");
	}

	// Next, directories.
	// if -d is set, just print their names. Otherwise, print their contents.
	if (opt_show_dir)
		print_entries(dirs, ".", num_dirs);
	else {
		for (int i = 0; i < num_dirs; i++) {
			if (argc > 1 && !opt_recurse) {
				// Print the dir name prior to the contents, if there are more
				// than one to show
				printf("%s:\n", dirs[i]);
			}
			do_dir(dirs[i]);

			// If more dirs remain, separate them with a newline
			if (num_dirs > i + 1 && !opt_recurse)
				printf("\n");
		}
	}


	// We're done! Start freeing memory.
	// (Though we ARE about to exit...)
	for (int i = 0; i < argc; i++) {
		free(st[i]);
	}

	free(dirs);
	free(files);
	free(st);

out_show_dir:
	free(names);

	fflush(stdout);

	return 0;
}
