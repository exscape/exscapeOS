#ifndef _SYSCALL_H
#define _SYSCALL_H

#include <types.h>

void init_syscalls(void);

#define DECL_SYSCALL0(fn, ret) ret syscall_##fn(void);
#define DECL_SYSCALL1(fn, ret, p1) ret syscall_##fn(p1);
#define DECL_SYSCALL2(fn, ret, p1,p2) ret syscall_##fn(p1,p2);
#define DECL_SYSCALL3(fn, ret, p1,p2,p3) ret syscall_##fn(p1,p2,p3);
#define DECL_SYSCALL4(fn, ret, p1,p2,p3,p4) ret syscall_##fn(p1,p2,p3,p4);
#define DECL_SYSCALL5(fn, ret, p1,p2,p3,p4,p5) ret syscall_##fn(p1,p2,p3,p4,p5);

#define DEFN_SYSCALL0(fn, ret, num) \
ret syscall_##fn(void) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num)); \
  return a; \
}

#define DEFN_SYSCALL1(fn, ret, num, P1) \
ret syscall_##fn(P1 p1) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((ret)p1)); \
  return a; \
}

#define DEFN_SYSCALL2(fn, ret, num, P1, P2) \
ret syscall_##fn(P1 p1, P2 p2) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((ret)p1), "c" ((ret)p2)); \
  return a; \
}

#define DEFN_SYSCALL3(fn, ret, num, P1, P2, P3) \
ret syscall_##fn(P1 p1, P2 p2, P3 p3) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((ret)p1), "c" ((ret)p2), "d"((ret)p3)); \
  return a; \
}

#define DEFN_SYSCALL4(fn, ret, num, P1, P2, P3, P4) \
ret syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((ret)p1), "c" ((ret)p2), "d" ((ret)p3), "S" ((ret)p4)); \
  return a; \
}

#define DEFN_SYSCALL5(fn, ret, num) \
ret syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((ret)p1), "c" ((ret)p2), "d" ((ret)p3), "S" ((ret)p4), "D" ((ret)p5)); \
  return a; \
}

DECL_SYSCALL0(_exit, int);
DECL_SYSCALL1(puts, int, const char *);
DECL_SYSCALL1(sleep, int, uint32);
DECL_SYSCALL0(getchar, int);
DECL_SYSCALL1(putchar, int, int);
DECL_SYSCALL2(open, int, const char *, int);
DECL_SYSCALL3(read, int, int, void *, int);
DECL_SYSCALL1(close, int, int);
DECL_SYSCALL1(malloc, void *, size_t);
DECL_SYSCALL1(free, int, void *);
struct stat;
DECL_SYSCALL2(stat, int, const char *, struct stat *);
DECL_SYSCALL1(chdir, int, const char *);
DECL_SYSCALL3(write, int, int, const void *, int);
DECL_SYSCALL2(fstat, int, int, struct stat *);
DECL_SYSCALL0(getpid, int);

#endif
