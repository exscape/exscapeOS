#include <_ansi.h>
#include <reent.h>

#ifdef __getreent
#undef __getreent
#endif

struct _reent *
_DEFUN_VOID(__getreent)
{
	// NULL is undeclared here
	static struct _reent *ptr = (struct _reent *)0;
	if (ptr == (struct _reent *)0)
		ptr = sys___getreent();

	return ptr;
}
