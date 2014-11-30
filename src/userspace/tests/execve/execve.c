#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>

int main(int argc, char **argv) {

	int pid = fork();
	if (pid == 0) {
		printf("In child; will execve now\n");
		setenv("ENVTEST", "Hello, world!", 1);
		int r = execve("/initrd/execve_child", argv, NULL);
		printf("execve returned!! return value: %d, errno = %d (%s)\n", r, errno, strerror(errno));
		_exit(0);
	}
	else if (pid > 0) {
		printf("In parent: waiting for child %d...\n", pid);
		int status;
		int r = wait(&status);
		printf("Parent: wait returned %d; status = %04x\n", r, status);
		printf("Exiting from parent.\n");
		exit(0);
	}
	
	fprintf(stderr, "fork failed!\n");

	return 1;
}
