#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char **argv) {
	setenv("ENVTEST", "It works!", 1);
	int r = fork();

	printf("%s: getenv: %s\n", r == 0 ? "child" : "parent", getenv("ENVTEST"));
	return 0;
}
