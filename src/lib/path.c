#include <sys/types.h>
#include <path.h>
#include <string.h>

static bool __path_join(char *path, const char *right) {
	if (path == NULL || right == NULL)
		return false;
	if (strlen(right) > 255)
		return false;

	if (strchr(right, '/') != NULL)
		return false; // TODO: shouldn't happen

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

bool path_join(char *path, const char *right) {
	char *tmp;
	char *token = NULL;
	char buf[PATH_MAX+1] = {0};
	strlcpy(buf, right, PATH_MAX+1);
	for (token = strtok_r(buf, "/", &tmp); token != NULL; token = strtok_r(NULL, "/", &tmp)) {
		if (!__path_join(path, token))
			return false;
	}

	return true;
}

// Removes . and .. from the path, converting it as necessary.
// For example, the input "/a/b/../c/./d" should become "/a/c/d".
bool path_collapse_dots(char *path) {
	if (*path != '/')
		return false;

	char buf[PATH_MAX+1] = {0};
	strcpy(buf, "/");

	char *tmp;
	char *token = NULL;
	for (token = strtok_r(path, "/", &tmp); token != NULL; (token = strtok_r(NULL, "/", &tmp))) {
		path_join(buf, token);
	}

	// The output string should never be longer than the input, so this should be OK.
	strcpy(path, buf);

	return true;
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
