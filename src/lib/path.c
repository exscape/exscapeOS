#include <types.h>
#include <path.h>
#include <string.h>

#define PATH_MAX 1023 /* buffers are 1024, so 1023 chars max */

bool path_join(char *path, const char *right) {
	if (path == NULL || right == NULL)
		return false;
	if (strlen(right) > 255)
		return false;

	if (strcmp(right, ".") == 0)
		return true; // We didn't do anything, but didn't fail either
	if (strcmp(right, "..") == 0) {
		// Move back one directory
		path_dirname(path);
		return true;
	}

	size_t path_len = strlen(path);
	if (path[path_len - 1] != '/' && right[0] != '/') {
		// Add a slash between the two
		path[path_len] = '/';
		path[path_len + 1] = 0;
	}

	bool trunc = (strlen(path) + strlen(right) > PATH_MAX);

	strlcat(path, right, PATH_MAX);

	return !trunc;
}

void path_dirname(char *path) {
	size_t path_len = strlen(path);
	if (path[path_len - 1] == '/')
		path[path_len - 1] = 0;

	char *p = strrchr(path, '/');
	if (p != NULL)
		*p = 0;

	if(path[0] == 0) {
		// If this happens, the parent is the root directory
		strcpy(path, "/");
	}
}

void path_basename(char *path) {
	size_t path_len = strlen(path);
	if (path[path_len - 1] == '/' && path_len != 1)
		path[path_len - 1] = 0;
	else if (path[path_len - 1] == '/' && path_len == 1) {
		// Path is /
		return;
	}

	char *p = strrchr(path, '/');

	char tmp[256] = {0}; // we don't have memmove(), so use a temporary buffer

	strlcpy(tmp, p + 1, 256);
	strlcpy(path, tmp, 256);
}
