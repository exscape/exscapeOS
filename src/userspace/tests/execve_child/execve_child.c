#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
	printf("in execve \"child\", attempting getenv...\n");
	const char *e = getenv("ENVTEST");
	if (e == NULL) {
		printf("getenv(\"ENVTEST\") == NULL!\n");
	}
	else if (*e == 0) {
		printf("getenv returned empty string\n");
	}
	else
		printf("getenv returned: %s\n", e);

	return 0;
}
