#if _EXSCAPEOS_USERSPACE

#include "types.h"

#define DECL_SYSCALL0(fn) int fn();
#define DECL_SYSCALL1(fn,p1) int fn(p1);
#define DECL_SYSCALL2(fn,p1,p2) int fn(p1,p2);
#define DECL_SYSCALL3(fn,p1,p2,p3) int fn(p1,p2,p3);
#define DECL_SYSCALL4(fn,p1,p2,p3,p4) int fn(p1,p2,p3,p4);
#define DECL_SYSCALL5(fn,p1,p2,p3,p4,p5) int fn(p1,p2,p3,p4,p5);

#define DEFN_SYSCALL0(fn, num) \
int fn() \
{ \
  int a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num)); \
  return a; \
}

#define DEFN_SYSCALL1(fn, num, P1) \
int fn(P1 p1) \
{ \
  int a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((int)p1)); \
  return a; \
}

#define DEFN_SYSCALL2(fn, num, P1, P2) \
int fn(P1 p1, P2 p2) \
{ \
  int a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((int)p1), "c" ((int)p2)); \
  return a; \
}

#define DEFN_SYSCALL3(fn, num, P1, P2, P3) \
int fn(P1 p1, P2 p2, P3 p3) \
{ \
  int a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((int)p1), "c" ((int)p2), "d"((int)p3)); \
  return a; \
}

#define DEFN_SYSCALL4(fn, num, P1, P2, P3, P4) \
int fn(P1 p1, P2 p2, P3 p3, P4 p4) \
{ \
  int a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((int)p1), "c" ((int)p2), "d" ((int)p3), "S" ((int)p4)); \
  return a; \
}

#define DEFN_SYSCALL5(fn, num) \
int fn(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5) \
{ \
  int a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((int)p1), "c" ((int)p2), "d" ((int)p3), "S" ((int)p4), "D" ((int)p5)); \
  return a; \
}

DECL_SYSCALL0(exit_proc);
DECL_SYSCALL1(puts, const char *);
DECL_SYSCALL1(sleep, uint32);
DECL_SYSCALL0(getchar);
DECL_SYSCALL1(putchar, int);
DECL_SYSCALL2(open, const char *, int);
DECL_SYSCALL3(read, int, void *, int);

#endif // _EXSCAPEOS_USERSPACE

DEFN_SYSCALL0(exit_proc, 0);
DEFN_SYSCALL1(puts, 1, const char *);
DEFN_SYSCALL1(sleep, 2, uint32);
DEFN_SYSCALL0(getchar, 3);
DEFN_SYSCALL1(putchar, 4, int);
DEFN_SYSCALL2(open, 5, const char *, int);
DEFN_SYSCALL3(read, 6, int, void *, int);
