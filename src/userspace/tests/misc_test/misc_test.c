#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <setjmp.h>

int main(int argc, char **argv) {

	FILE *f = fopen("/initrd/initrd_test.txt", "r");
	if (!f) {
		perror("fopen");
		exit(1);
	}

	printf("opened file, forking...\n");

	pid_t pid = fork();
	if (pid == 0) {
		// Child
		printf("Child: reading part of the file...\n");
	}
	else if (pid > 0) {
		// Parent
		printf("Parent: doing my part\n");
	}
		char buf[9] = {0};
		fread(buf, 1, 8, f);
		printf("%s", buf);
		fflush(stdout);

		exit(0);

        return 0;
}

