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
	else if (n == 0) {
		out[0] = 0x30;
		out[1] = 0;
		return;
	}

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

/* A nice, simple, and probably not-too-great PRNG */

static unsigned long int next = 1;

int rand(void) // RAND_MAX assumed to be 32767
{
    next = next * 1103515245 + 12345;
    return (unsigned int)(next/65536) % 32768;
}

void srand(unsigned int seed)
{
    next = seed;
}

int isdigit(int c) {
	if (c >= '0' && c <= '9')
		return 1;
	else
		return 0;
}

/* Calculates base^exp - integers only. No negative exponents. */
int ipow(int base, int exp) {
	if (base == 0)
		return 0; // 0^anything is 0
	if (exp < 0)
		return 0; // we don't support negative exponents
	if (exp == 0)
		return 1; // anything^0 is 1 (except 0^0, handled above)

	int result = 1;
	while (exp--)
		result *= base;

	return result;
}

/* Converts a char array to an integer. */
int atoi(const char *str) {
	if (*str == 0)
		return 0;

	int num = 0;

	/* skip non-number data */
	while (*str && !isdigit(*str) && *str != '-') str++;

	/* support negative numbers */
	int sign = 1;
	if (*str == '-') {
		sign = -1;
		str++;
	}

	/* calculate the end of the actual number data (other data is allowed and ignored) */
	const char *end = str;
	while (isdigit(*end)) end++;

	/* calculate the length */
	size_t len = (end - str);

	/* Calculate the actual number */
	const char *p = str;
	for (size_t i = 0; i < len; i++) {
		num += (*p++ - 0x30) * ipow(10, (len - i - 1));
	}

	/* Flip the sign (multiply by -1) if needed */
	num *= sign;

	return num;
}
