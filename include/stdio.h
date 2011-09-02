#ifndef _STDIO_H
#define _STDIO_H

#include <stdarg.h>

int vsprintf(char *buf, const char *fmt, __gnuc_va_list args);

#endif
