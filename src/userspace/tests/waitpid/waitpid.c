#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv) {
	int pid = fork();
	if (pid == 0) {
		// Child
		printf("in child, pid = %d, ppid = %d\n", getpid(), getppid());
		sleep(2);
	}
	else if (pid > 0) {
		// Parent
		printf("in parent, pid = %d, child pid = %d\n", getpid(), pid);
		int status;
		int ret;
		do { ret = waitpid(pid, &status, WNOHANG); printf("."); fflush(stdout);} while (ret == 0);
		if (ret == -1) {
			printf("waitpid failed: errno = %d (%s)\n", errno, strerror(errno));
		}
		printf("waitpid returned %d, status = %04x\n", ret, status);
	}
	else {
		fprintf(stderr, "fork() failed!\n");
		exit(1);
	}

	printf("exiting from %s\n", pid == 0 ? "child" : "parent");

	return 0;
}
