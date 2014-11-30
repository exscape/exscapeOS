#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
	const char *a = "Hello, world!";
	setenv("VAR", a, 1);

	printf("getenv(\"VAR\") = %s\n", getenv("VAR"));
	setenv("VAR", "Yoooooooooooo", 1);
	printf("getenv(\"VAR\") = %s\n", getenv("VAR"));
	setenv("VAR", "Nice", 1);
	printf("getenv(\"VAR\") = %s\n", getenv("VAR"));

	return 0;
}
