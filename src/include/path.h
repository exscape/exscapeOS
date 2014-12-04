#ifndef _PATH_H
#define _PATH_H

#define PATH_MAX 1023 /* buffers are 1024, so 1023 chars max */

bool path_join(char *path, const char *right);
bool path_collapse_dots(char *path);
void path_dirname(char *path);
void path_basename(char *path);

#endif
