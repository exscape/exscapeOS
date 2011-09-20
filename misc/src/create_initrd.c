#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef unsigned int uint32;
typedef signed int sint32;

/*
 * A small helper application that creates a initrd image used by the OS.
 * The "on-disk" format is simple;
 * 4 bytes: int containing the number of files
 * 64 headers describing the files and their locations (see above for how many are actually used)
 * The files themselves, mashed together
 */

struct initrd_header {
	unsigned char magic;
	char name[64];
	uint32 offset; /* how far into the initrd image this file is located */
	uint32 length; /* size of the file, in bytes */
};

int main(int argc, char *argv[]) {
	sint32 nheaders = argc - 1;
	if (nheaders <= 0) {
		printf("Usage: %s <file 1> [file 2] ... [file n]\n", argv[0]);
		exit(-1);
	}
	struct initrd_header headers[64];
	memset(&headers, 0, 64 * sizeof(struct initrd_header));

	uint32 offset = sizeof(struct initrd_header) * 64 + sizeof(sint32);

	/* Set up the headers */
	for (uint32 i = 0; i < nheaders; i++) {
		printf("writing file %s at 0x%x\n", argv[i+1], offset);

		/* Remove the path path from the filename stored */
		char *tmp = strrchr(argv[i+1], '/');
		if (tmp != NULL)
			strcpy(headers[i].name, tmp + 1); /* + 1 to skip the actual / */
		else
			strcpy(headers[i].name, argv[i+1]);

		headers[i].offset = offset;
		FILE *inf = fopen(argv[i+1], "r");

		if (inf == NULL) {
			fprintf(stderr, "Unable to open file \"%s\"!\n", argv[i+1]);
			exit(-1);
		}

		fseek(inf, 0, SEEK_END);
		headers[i].length = ftell(inf);
		offset += headers[i].length;
		fclose(inf);

		headers[i].magic = 0xbf;
	}

	/* Read and write out the actual data */
	FILE *outf = fopen("initrd.img", "w");
	unsigned char *data = (unsigned char *)malloc(offset);
	fwrite(&nheaders, sizeof(sint32), 1, outf);
	fwrite(headers, sizeof(struct initrd_header), 64, outf);

	/* Write all the file data */
	for (uint32 i = 0; i < nheaders; i++) {
		FILE *inf = fopen(argv[i+1], "r"); /* "can't" be NULL, since we've already opened them earlier on */
		fread(data, 1, headers[i].length, inf);
		fwrite(data, 1, headers[i].length, outf);
		fclose(inf);
	}

	free(data);
	fclose(outf);

	return 0;
}
