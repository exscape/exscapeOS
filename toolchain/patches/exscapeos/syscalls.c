#include <sys/stat.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/times.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <dirent.h>
#include <stdio.h>
#include <limits.h>
#include <reent.h>
#include <unistd.h>
#include <stdlib.h>

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

DECL_SYSCALL1(exit, void, int);
DECL_SYSCALL1(puts, int, const char *);
DECL_SYSCALL1(sleep, int, uint32);
DECL_SYSCALL0(getchar, int);
DECL_SYSCALL1(putchar, int, int);
DECL_SYSCALL2(open, int, const char *, int);
DECL_SYSCALL3(read, int, int, void *, size_t);
DECL_SYSCALL1(close, int, int);
//DECL_SYSCALL1(malloc, void *, size_t);
//DECL_SYSCALL1(free, int, void *);
DECL_SYSCALL2(stat, int, const char *, struct stat *);
DECL_SYSCALL1(chdir, int, const char *);
DECL_SYSCALL3(write, int, int, const char *, int);
DECL_SYSCALL2(fstat, int, int, struct stat *);
DECL_SYSCALL0(getpid, int);
DECL_SYSCALL1(sbrk, void *, ptrdiff_t); // TODO: return type caddr_t
DECL_SYSCALL0(__getreent, struct _reent *);
DECL_SYSCALL3(getdents, int, int, void *, int);
DECL_SYSCALL2(gettimeofday, int, struct timeval *, void *);
DECL_SYSCALL0(fork, int);
DECL_SYSCALL2(nanosleep, int, const struct timespec *, struct timespec *);
DECL_SYSCALL1(wait, int, int *);
DECL_SYSCALL0(getppid, int);
DECL_SYSCALL3(waitpid, int, int, int *, int);
DECL_SYSCALL3(execve, int, const char *, char * const *, char * const *);
DECL_SYSCALL2(getcwd, char *, char *, size_t);
DECL_SYSCALL1(dup, int, int);
DECL_SYSCALL2(dup2, int, int, int);
DECL_SYSCALL1(pipe, int, int *);
DECL_SYSCALL2(lstat, int, const char *, struct stat *);
//DECL_SYSCALL3(readlink, ssize_t, const char *, char *, size_t);

void sys__exit(int status) {
	asm volatile("int $0x80" : : "a" (0), "b" ((int)status));
}

/* exit above is syscall 0 */
DEFN_SYSCALL1(puts, int, 1, const char *);
DEFN_SYSCALL1(sleep, int, 2,uint32);
DEFN_SYSCALL0(getchar, int, 3);
DEFN_SYSCALL1(putchar, int, 4, int);
DEFN_SYSCALL2(open, int, 5, const char *, int);
DEFN_SYSCALL3(read, int, 6, int, void *, size_t);
DEFN_SYSCALL1(close, int, 7, int);
//DEFN_SYSCALL1(malloc, void *, 8, size_t);
//DEFN_SYSCALL1(free, int, 9, void *);
DEFN_SYSCALL2(stat, int, 10, const char *, struct stat *);
DEFN_SYSCALL1(chdir, int, 11, const char *);
DEFN_SYSCALL3(write, int, 12, int, const char *, int);
/* lseek is syscall 13, see below */
DEFN_SYSCALL2(fstat, int, 14, int, struct stat *);
DEFN_SYSCALL0(getpid, int, 15);
DEFN_SYSCALL1(sbrk, void *, 16, ptrdiff_t);
DEFN_SYSCALL0(__getreent, struct _reent *, 17);
DEFN_SYSCALL3(getdents, int, 18, int, void *, int);
DEFN_SYSCALL2(gettimeofday, int, 19, struct timeval *, void *);
DEFN_SYSCALL0(fork, int, 20);
DEFN_SYSCALL2(nanosleep, int, 21, const struct timespec *, struct timespec *);
DEFN_SYSCALL1(wait, int, 22, int *);
DEFN_SYSCALL0(getppid, int, 23);
DEFN_SYSCALL3(waitpid, int, 24, int, int *, int);
DEFN_SYSCALL3(execve, int, 25, const char *, char * const *, char * const *);
DEFN_SYSCALL2(getcwd, char *, 26, char *, size_t);
DEFN_SYSCALL1(dup, int, 27, int);
DEFN_SYSCALL2(dup2, int, 28, int, int);
DEFN_SYSCALL1(pipe, int, 29, int *);
DEFN_SYSCALL2(lstat, int, 30, const char *, struct stat *);
DEFN_SYSCALL3(readlink, ssize_t, 31, const char *, char *, size_t);

// When adding a syscall, don't forget to also add it to src/kernel/syscall.c!

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

int getppid(void) {
	return sys_getppid();
}

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

int chdir(const char *path) {
	int r = sys_chdir(path);
	if (r == 0)
		return 0;
	else {
		errno = -r;
		return -1;
	}
}

void _exit(int status) {
	sys__exit(status);
	for(;;) { } // silence noreturn warning
}

int close(int file) {
	int r = sys_close(file);
	if (r != 0) {
		errno = -r;
		return -1;
	}
}

int _execve(const char *name, char * const *argv, char * const *env) {
	return execve(name, argv, env);
}

int execve(const char *name, char * const *argv, char * const *env) {
	int r = sys_execve(name, argv, env == NULL ? environ : env);
	if (r != 0) {
		errno = -r;
		return -1;
	}
}

ssize_t readlink(const char *pathname, char *buf, size_t bufsiz) {
	int r = sys_readlink(pathname, buf, bufsiz);
	if (r <= 0) {
		errno = -r;
		return -1;
	}
	else
		return r;

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

int link(const char *old, const char *new) {
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

int read(int file, void *ptr, size_t len) {
	if (file < 0) {
		errno = -EBADF;
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

void *sbrk(ptrdiff_t incr) {
	void *ret = (void *)sys_sbrk(incr);
	if ((signed long)ret < 0 && (signed long)ret > -200) {
		errno = - ((int)ret);
		return (void *)(-1);
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

int lstat(const char *file, struct stat *st) {
	if (st == NULL) {
		errno = EFAULT;
		return -1;
	}

	int ret = sys_lstat(file, st);
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

int unlink(const char *name) {
	// TODO: unlink!
	errno = ENOENT;
	return -1;
}

int wait(int *status) {
	int ret;
	if ((ret = sys_wait(status)) >= 0) {
		return ret;
	}
	else {
		errno = -ret;
		return -1;
	}
}

int waitpid(int pid, int *status, int options) {
	int ret;
	if ((ret = sys_waitpid(pid, status, options)) >= 0) {
		return ret;
	}
	else {
		errno = -ret;
		return -1;
	}
}

int write(int file, const void *ptr, size_t len) {
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
	int ret;
	if ((ret = sys_gettimeofday(p, __tz)) == 0)
		return ret;
	else {
		errno = -ret;
		return -1;
	}
}

int getdents(int fd, void *dp, int count) {
	int ret;
	if ((ret = sys_getdents(fd, dp, count)) >= 0) {
		return ret;
	}
	else {
		errno = -ret;
		return -1;
	}
}

int fork(void) {
	int ret;
	if ((ret = sys_fork()) >= 0)
		return ret;
	else {
		errno = -ret;
		return -1;
	}
}

int pipe(int fildes[2]) {
	int ret;
	if ((ret = sys_pipe(fildes)) == 0)
		return 0;
	else {
		errno = -ret;
		return -1;
	}
}

// Ugly, but doing this appears fairly common-ish.
int vfork(void)
{
  int pid = fork();
	if (pid == 0) {
		/* In child. */
		return 0;
	}
	else {
		/* In parent.  Wait for child to finish. */
		return waitpid(pid, NULL, 0);
	}
}

int dup(int fd) {
	return sys_dup(fd);
}

int dup2(int fd, int fd2) {
	return sys_dup2(fd, fd2);
}

int nanosleep(const struct timespec *rqtp, struct timespec *rmtp) {
	int ret;
	if ((ret = sys_nanosleep(rqtp, rmtp)) != 0) {
		errno = -ret;
		return -1;
	}
	else
		return 0;
}

char *getcwd(char *__buf, size_t __size) {
	if (__buf != NULL && __size > 0) {
		// User specified a proper buffer: use it, and give up if it fails
		char *ret = sys_getcwd(__buf, __size);
		int r = (int)ret;
		if (r < 0 && r > -150) {
			// Treat as errno
			errno = -r;
			return NULL;
		}
		else
			return ret;
	}
	else if (__size == 0 && __buf != NULL) {
		// size == 0 is invalid; however we ignore it if buf == NULL
		errno = EINVAL;
		return NULL;
	}
	else if (__buf == NULL) {
		// Allocate a buffer to use
		char *buf = malloc(PATH_MAX + 1);
		char *ret = sys_getcwd(buf, PATH_MAX + 1);
		if ((int)ret < 0 && (int)ret > -150) {
			free(buf);
			errno = -((int)ret);
			return NULL;
		}
		else
			return ret;
	}
}
