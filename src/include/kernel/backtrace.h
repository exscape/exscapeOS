#ifndef _BACKTRACE_H
#define _BACKTRACE_H

#include <sys/types.h>

struct symbol {
	uint32 eip;
	const char *name;
};

struct symbol *addr_to_func(uint32 eip);

#define BACKTRACE_MAX 16
// Lower indexes = deeper nesting
struct backtrace {
	uint32 eip[BACKTRACE_MAX];
};

void get_backtrace(uint32 _ebp, struct backtrace *bt);
void print_backtrace_struct(struct backtrace *bt);
void print_backtrace(void);
void print_backtrace_ebp(uint32 _ebp);

#endif
