#include <stdlib.h>
#include <string.h>

void reverse(char *s) {
	/* Reverses the bytes of a string. The string must be NULL-terminated. */
	char tmp;
	size_t j = strlen(s) - 1;
	for (size_t i=0; i < j; i++, j--) {
		tmp = s[j];
		s[j] = s[i];
		s[i] = tmp;
	}
}

void itoa(signed long n, char *out) {
	/* Converts a signed long to a char[] representation. */
	/* Make sure the 'out' parameter is large enough! */
	/* 21 bytes should be enough even for 64-bit signed longs. */

	signed long sign = n;

	if (n < 0)
		n = -n;

	signed long i;
	for (i = 0; n > 0 /* [sic] */; i++) {
		out[i] = (n % 10) + 0x30; /* dec -> ASCII */
		n /= 10;
	}
	if (sign < 0)
		out[i++] = '-';

	out[i++] = 0;
	
	reverse(out);
}
