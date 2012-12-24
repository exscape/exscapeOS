#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int main(int argc, char **argv) {

	int ret = fork();
	if (ret == -1) {
		printf("fork failed with error %d: %s\n", errno, strerror(errno));
		return 1;
	}
	else if (ret == 0) {
		printf("fork(): in child! pid = %d\n", getpid());
		sleep(1);
	}
	else {
		printf("fork(): in parent; child has pid %d\n", ret);
		sleep(2);
	}

	printf("Exiting from %s\n", ret == 0 ? "child" : "parent");

	return 0;
}
