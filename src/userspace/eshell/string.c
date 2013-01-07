#include <sys/types.h>
#include <string.h>
#include <stdlib.h>

// Replaces the first occurance of old with new in buf.
// size is the size of the buffer, in bytes.
void str_replace(char *buf, const char *old, const char *new, int size) {
	char *start = strstr(buf, old);
	if (start == NULL)
		return;

	ssize_t buf_len = (ssize_t)strlen(buf);
	ssize_t old_len = (ssize_t)strlen(old);
	ssize_t new_len = (ssize_t)strlen(new);

	if (buf_len + (new_len - old_len) > size - 1) {
		// This won't fit!
		return;
	}

	char *end = start + old_len;
	char *ending = malloc((buf + buf_len) - end + 1);
	strlcpy(ending, end, (buf + buf_len) - end + 1);

	// OK, we have the stuff after the variable stored away; now we're free
	// to destroy the buffer contents after /end/ with no ill consequences.

	// Do the replace
	int t = (size - (start - buf));
	strlcpy(start, new, t);
	// Copy back the ending
	strlcat(start + new_len, ending, (size - new_len));

	free(ending);
}

