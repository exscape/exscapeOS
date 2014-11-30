#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

int main(int argc, char **argv) {
#define BSIZE 100

	char buf[BSIZE] = {0};
	int fildes[2];
	ssize_t nbytes;
	int status;

	status = pipe(fildes);
	if (status == -1 ) {
		/* an error occurred */
		fprintf(stderr, "pipe: error %d (%s)\n", errno, strerror(errno));
		exit(1);
	}

	switch (fork()) {
	case -1: /* Handle error */
		break;

	case 0:  /* Child - reads from pipe */
		close(fildes[1]);                       /* Write end is unused */
		nbytes = read(fildes[0], buf, BSIZE);   /* Get data from pipe */
		if (nbytes == -1) {
			perror("read");
			exit(EXIT_FAILURE);
		}
		close(fildes[0]);                       /* Finished with pipe */
		printf("read from pipe: %s", buf);
		assert(read(fildes[0], buf, BSIZE) <= 0);
		exit(EXIT_SUCCESS);
		break;

	default:  /* Parent - writes to pipe */
		close(fildes[0]);                       /* Read end is unused */
		nbytes = write(fildes[1], "Hello world\n", 12);  /* Write data on pipe */
		if (nbytes == -1) {
			perror("parent: write");
			exit(EXIT_FAILURE);
		}
		close(fildes[1]);                       /* Child will see EOF */
		exit(EXIT_SUCCESS);
		break;
	}

	return 0;
}
