#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>

// Program to test symlinks.
// Will only work for me, as it depends on a very special directory structure being present!
// I figured a "test suite" is better than manually testing a bunch of random stuff
// every time I change something that might break.

int successes = 0;
int failures = 0;

#define assert(b) ((b) ? (void)0 : panic_assert(__FILE__, __LINE__, #b))
void panic_assert(const char *file, uint32 line, const char *desc) {
	fprintf(stderr, "Assertion failure: %s:%d: %s\n", file, line, desc);
	failures++;
}

void test_open_read(const char *path, int expected_errno) {
	char buf[1024] = {0};
	printf("testing open+read on %s... ", path);
	int fd = open(path, O_RDONLY);
	if (fd < 0 && expected_errno == 0) {
		printf("failed! error = %s, expected success\n", strerror(errno));
		failures++;
	}
	else if (fd < 0 && expected_errno > 0) {
		if (errno != expected_errno) {
			printf("failed! error was %s, expected %s", strerror(errno), strerror(expected_errno));
			failures++;
		}
		else {
			printf("failed as expected (%s)!\n", strerror(errno));
			successes++;
		}
	}
	else if (fd >= 0 && expected_errno != 0) {
		printf("success, expected failure (%s)!\n", strerror(expected_errno));
		failures++;
	}
	else {
		assert(fd >= 0);
		assert(expected_errno == 0);
		printf("success!\n");
		successes++;
	}

	if (fd >= 0 && expected_errno == 0)
		assert(read(fd, buf, 1023) >= 0);

	close(fd);
}

void test_opendir_getdents(const char *path, int expected_errno) {
	printf("testing opendir+getdents on %s... ", path);
	DIR *dir = opendir(path);

	if (dir == NULL && expected_errno == 0) {
		printf("failed! error = %s, expected success\n", strerror(errno));
		failures++;
	}
	else if (dir == NULL && expected_errno > 0) {
		if (errno != expected_errno) {
			printf("failed! error was %s, expected %s", strerror(errno), strerror(expected_errno));
			failures++;
		}
		else {
			printf("failed as expected (%s)!\n", strerror(errno));
			successes++;
		}
	}
	else if (dir && expected_errno != 0) {
		printf("success, expected failure (%s)!\n", strerror(expected_errno));
		failures++;
	}
	else {
		assert(dir != NULL);
		assert(expected_errno == 0);
		printf("success!\n");
		successes++;
	}

	if (dir && expected_errno == 0)
		assert(readdir(dir) != NULL);

	if (dir)
		closedir(dir);
}

void test_readlink(const char *path, int expected_errno, const char *expected_value) {
	printf("testing readlink on %s... ", path);
	char buf[1024] = {0};
	int ret = readlink(path, buf, 1024);

	if (ret <= 0 && expected_errno == 0) {
		printf("failed! error = %s, expected success\n", strerror(errno));
		failures++;
	}
	else if (ret <= 0 && expected_errno > 0) {
		if (errno != expected_errno) {
			printf("failed! error was %s, expected %s", strerror(errno), strerror(expected_errno));
			failures++;
		}
		else {
			printf("failed as expected (%s)!\n", strerror(errno));
			successes++;
		}
	}
	else if (ret >=0 && expected_errno != 0) {
		printf("success, expected failure (%s)!\n", strerror(expected_errno));
		failures++;
	}
	else {
		assert(ret > 0);
		assert(expected_errno == 0);

		if (expected_value) {
			if (strcmp(buf, expected_value) == 0) {
				successes++;
				printf("success! value matches\n");
			}
			else {
				failures++;
				printf("failed! value did not match!\n");
			}
		}
	}
}

int main(int argc, char **argv) {
	setvbuf(stdout, NULL, _IONBF, 0);
	assert(chdir("/ext2") == 0);

	test_open_read("abslinketc", 0);
	test_open_read("abslinkroot", 0);
	test_open_read("nestlink/5/mountslink", 0);
	test_open_read("nestedlinksdir/5/file", 0);

	test_open_read("nestedlinksdir/1/file", ELOOP);
	test_open_read("inf1", ELOOP);
	test_open_read("broken/relfile", ENOENT);

	test_opendir_getdents("bin", 0);
	test_opendir_getdents("nestedlinksdir/6", 0);

	test_opendir_getdents("nestedlinksdir/2", ELOOP);
	test_opendir_getdents("inf1", ELOOP);
	test_opendir_getdents("broken/absdir", ENOENT);

	test_readlink("longlink", 0, "this_is_a_very_long_link_value/which_certainly_is_longer_than_the_60_char_limit_on_fast_symbolic_links");
	test_readlink("shortlink_absolute", 0, "/bin/ls");
	test_readlink("nestedlinksdir", EINVAL, NULL);
	test_readlink("broken/absfile", 0, "/doesnotexist");

	struct stat st;

	printf("testing stat on shortlink_relative... ");
	int ret = stat("shortlink_relative", &st);
	if (ret != 0) {
		printf("failed, error = %s\n", strerror(errno));
		failures++;
	}
	else {
		if (st.st_size == 292) {
			printf("success\n");
			successes++;
		}
		else {
			printf("failed! size != 292\n");
			failures++;
		}
	}

	printf("testing lstat on shortlink_relative... ");
	ret = lstat("shortlink_relative", &st);
	if (ret != 0) {
		printf("failed, error = %s\n", strerror(errno));
		failures++;
	}
	else {
		if (st.st_size == 7) {
			printf("success\n");
			successes++;
		}
		else {
			printf("failed! size != 7\n");
			failures++;
		}
	}

	printf("done testing! %d successes, %d failures\n", successes, failures);

	return failures; // hopefully equivalent to return 0
}
