#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
	pid_t pid;
	pid = fork();
	if (pid < 0) {
		printf("fork error!\n");
		exit(1);
	}
	else if (pid > 0) {
		printf("fork successful; parent exiting\n");
		exit(0);
	}

	printf("in child: parent is long gone. child pid = %d\n", getpid());
	printf("child: sleeping 5 seconds...\n");
	sleep(5);
	printf("child: returning 0\n");

	return 0;
}
