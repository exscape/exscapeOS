#include <sys/stat.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/times.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <stdio.h>
#include <limits.h>
#include <reent.h>

typedef signed   char  sint8;
typedef unsigned char  uint8;
typedef signed   short sint16;
typedef unsigned short uint16;
typedef signed   int   sint32;
typedef unsigned int   uint32;
typedef signed long long sint64;
typedef unsigned long long uint64;

#define DECL_SYSCALL0(fn, ret) ret sys_##fn(void);
#define DECL_SYSCALL1(fn, ret, p1) ret sys_##fn(p1);
#define DECL_SYSCALL2(fn, ret, p1,p2) ret sys_##fn(p1,p2);
#define DECL_SYSCALL3(fn, ret, p1,p2,p3) ret sys_##fn(p1,p2,p3);
#define DECL_SYSCALL4(fn, ret, p1,p2,p3,p4) ret sys_##fn(p1,p2,p3,p4);
#define DECL_SYSCALL5(fn, ret, p1,p2,p3,p4,p5) ret sys_##fn(p1,p2,p3,p4,p5);

#define DEFN_SYSCALL0(fn, ret, num) \
ret sys_##fn(void) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num)); \
  return a; \
}

#define DEFN_SYSCALL1(fn, ret, num, P1) \
ret sys_##fn(P1 p1) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((ret)p1)); \
  return a; \
}

#define DEFN_SYSCALL2(fn, ret, num, P1, P2) \
ret sys_##fn(P1 p1, P2 p2) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((ret)p1), "c" ((ret)p2)); \
  return a; \
}

#define DEFN_SYSCALL3(fn, ret, num, P1, P2, P3) \
ret sys_##fn(P1 p1, P2 p2, P3 p3) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((ret)p1), "c" ((ret)p2), "d"((ret)p3)); \
  return a; \
}

#define DEFN_SYSCALL4(fn, ret, num, P1, P2, P3, P4) \
ret sys_##fn(P1 p1, P2 p2, P3 p3, P4 p4) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((ret)p1), "c" ((ret)p2), "d" ((ret)p3), "S" ((ret)p4)); \
  return a; \
}

#define DEFN_SYSCALL5(fn, ret, num) \
ret sys_##fn(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((ret)p1), "c" ((ret)p2), "d" ((ret)p3), "S" ((ret)p4), "D" ((ret)p5)); \
  return a; \
}

DECL_SYSCALL0(_exit, void);
DECL_SYSCALL1(puts, int, const char *);
DECL_SYSCALL1(sleep, int, uint32);
DECL_SYSCALL0(getchar, int);
DECL_SYSCALL1(putchar, int, int);
DECL_SYSCALL2(open, int, const char *, int);
DECL_SYSCALL3(read, int, int, char *, int);
DECL_SYSCALL1(close, int, int);
//DECL_SYSCALL1(malloc, void *, size_t);
//DECL_SYSCALL1(free, int, void *);
DECL_SYSCALL2(stat, int, const char *, struct stat *);
DECL_SYSCALL1(chdir, int, const char *);
DECL_SYSCALL3(write, int, int, const char *, int);
DECL_SYSCALL2(fstat, int, int, struct stat *);
DECL_SYSCALL0(getpid, int);
DECL_SYSCALL1(sbrk, void *, sint32); // TODO: return type caddr_t
DECL_SYSCALL0(__getreent, struct _reent *);

//DEFN_SYSCALL0(_exit, void, 0);
void sys__exit(void) {
	asm volatile("int $0x80" : : "a" (0 /* syscall number */));
}

DEFN_SYSCALL1(puts, int, 1, const char *);
DEFN_SYSCALL1(sleep, int, 2,uint32);
DEFN_SYSCALL0(getchar, int, 3);
DEFN_SYSCALL1(putchar, int, 4, int);
DEFN_SYSCALL2(open, int, 5, const char *, int);
DEFN_SYSCALL3(read, int, 6, int, char *, int);
DEFN_SYSCALL1(close, int, 7, int);
//DEFN_SYSCALL1(malloc, void *, 8, size_t);
//DEFN_SYSCALL1(free, int, 9, void *);
DEFN_SYSCALL2(stat, int, 10, const char *, struct stat *);
DEFN_SYSCALL1(chdir, int, 11, const char *);
DEFN_SYSCALL3(write, int, 12, int, const char *, int);
/* lseek is syscall 13! */
DEFN_SYSCALL2(fstat, int, 14, int, struct stat *);
DEFN_SYSCALL0(getpid, int, 15);
DEFN_SYSCALL1(sbrk, void *, 16, sint32);
DEFN_SYSCALL0(__getreent, struct _reent *, 17);

sint64 sys_lseek(int fd, sint64 offset, int whence) {
	union {
		sint64 o64;
		uint32 u32[2];
	} arg, ret;
	arg.o64 = offset;
	asm volatile("int $0x80" : "=a" (ret.u32[0]), "=d" (ret.u32[1]) : "0" (13), "b" (fd), "c" (arg.u32[0]), "d"(arg.u32[1]), "S"(whence));
	return ret.o64;
}

/****************
 *** WRAPPERS ***
 ****************/

off_t lseek(int fd, off_t offset, int whence) {
	sint64 ret;
	ret = sys_lseek(fd, offset, whence);

	if (ret > 0) {
		return ret;
	}
	else {
		errno = (int)(-ret);
		return -1;
	}
}

void _exit(void) {
	sys__exit();
}

int close(int file) {
	int r = sys_close(file);
	if (r != 0) {
		errno = -r;
		return -1;
	}
}

int execve(char *name, char **argv, char **env) {
	// TODO: execve!
	errno = ENOMEM;
	return -1;
}

int fork(void) {
	// TODO: fork!
	errno = EAGAIN;
	return -1;
}

int fstat(int file, struct stat *st) {
	if (st == NULL) {
		errno = EFAULT;
		return -1;
	}

	int ret = sys_fstat(file, st);
	if (ret == 0)
		return 0;
	else {
		errno = -ret;
		return -1;
	}
}

int getpid(void) {
	return sys_getpid();
}

int isatty(int file) {
	if (file < 0) {
		errno = EBADF;
		return 0; // [sic]
	}
	struct stat st;
	int ret;
	if ((ret = fstat(file, &st)) == 0)
		return (S_ISCHR(st.st_mode)) ? 1 : 0;
	else {
		errno = -ret;
		return 0; // [sic]
	}
}

int kill(int pid, int sig) {
	// TODO: kill!
	errno = EINVAL;
	return -1;
}

int link(char *old, char *new) {
	// TODO: link!
	errno = EMLINK;
	return -1;
}

int open(const char *name, int flags, ...) {
	if (flags != O_RDONLY) {
		errno = EPERM; // TODO: allow writing
		return -1;
	}

	int ret;
	if ((ret = sys_open(name, flags)) < 0) {
		errno = -ret;
		return -1;
	}
	else
		return ret;
}

int read(int file, char *ptr, int len) {
	if (file < 0) {
		errno = EBADF;
		return -1;
	}

	if (file == 0 && isatty(1))
		fflush(stdout);

	int ret;
	if ((ret = sys_read(file, ptr, len)) < 0) {
		errno = -ret;
		return -1;
	}
	else
		return ret;
}

caddr_t sbrk(int incr) {
	void *ret = (void *)sys_sbrk(incr);
	if ((signed long)ret < 0 && (signed long)ret > -200) {
		// TODO: hack! Assume this region is invalid, and use it for errno returns
		errno = - ((int)ret);
		return (caddr_t)(-1);
	}
	else {
		return ret;
	}
}

int stat(const char *file, struct stat *st) {
	if (st == NULL) {
		errno = EFAULT;
		return -1;
	}

	int ret = sys_stat(file, st);
	if (ret == 0)
		return 0;
	else {
		errno = -ret;
		return -1;
	}
}

clock_t times(struct tms *buf) {
	// TODO: times!
	return -1;
}

int unlink(char *name) {
	// TODO: unlink!
	errno = ENOENT;
	return -1;
}

int wait(int *status) {
	// TODO: wait!
	errno = ECHILD;
	return -1;
}

int write(int file, char *ptr, int len) {
	if (file < 0) {
		errno = EBADF;
		return -1;
	}
	int ret;
	if ((ret = sys_write(file, ptr, len)) < 0) {
		errno = -ret;
		return -1;
	}
	else
		return ret;
}

int gettimeofday(struct timeval *p, void *__tz) {
	// TODO: gettimeofday!
	errno = EINVAL; // TODO: is this OK?
	return -1;
}

char *__env[1] = { 0 };
char **environ = __env;
