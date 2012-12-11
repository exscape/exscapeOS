#if _EXSCAPEOS_USERSPACE

#include "types.h"

struct stat;

#define DECL_SYSCALL0(fn, ret) ret fn(void);
#define DECL_SYSCALL1(fn, ret, p1) ret fn(p1);
#define DECL_SYSCALL2(fn, ret, p1,p2) ret fn(p1,p2);
#define DECL_SYSCALL3(fn, ret, p1,p2,p3) ret fn(p1,p2,p3);
#define DECL_SYSCALL4(fn, ret, p1,p2,p3,p4) ret fn(p1,p2,p3,p4);
#define DECL_SYSCALL5(fn, ret, p1,p2,p3,p4,p5) ret fn(p1,p2,p3,p4,p5);

#define DEFN_SYSCALL0(fn, ret, num) \
ret fn(void) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num)); \
  return a; \
}

#define DEFN_SYSCALL1(fn, ret, num, P1) \
ret fn(P1 p1) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((ret)p1)); \
  return a; \
}

#define DEFN_SYSCALL2(fn, ret, num, P1, P2) \
ret fn(P1 p1, P2 p2) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((ret)p1), "c" ((ret)p2)); \
  return a; \
}

#define DEFN_SYSCALL3(fn, ret, num, P1, P2, P3) \
ret fn(P1 p1, P2 p2, P3 p3) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((ret)p1), "c" ((ret)p2), "d"((ret)p3)); \
  return a; \
}

#define DEFN_SYSCALL4(fn, ret, num, P1, P2, P3, P4) \
ret fn(P1 p1, P2 p2, P3 p3, P4 p4) \
{ \
  ret a; \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((ret)p1), "c" ((ret)p2), "d" ((ret)p3), "S" ((ret)p4)); \
  return a; \
}

#define DEFN_SYSCALL5(fn, ret, num) \
ret fn(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5) \
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
DECL_SYSCALL2(stat, int, const char *, struct stat *);
DECL_SYSCALL1(chdir, int, const char *);
DECL_SYSCALL3(write, int, int, const void *, int);

#endif // _EXSCAPEOS_USERSPACE

DEFN_SYSCALL0(_exit, int, 0);
DEFN_SYSCALL1(puts, int, 1, const char *);
DEFN_SYSCALL1(sleep, int, 2,uint32);
DEFN_SYSCALL0(getchar, int, 3);
DEFN_SYSCALL1(putchar, int, 4, int);
DEFN_SYSCALL2(open, int, 5, const char *, int);
DEFN_SYSCALL3(read, int, 6, int, void *, int);
DEFN_SYSCALL1(close, int, 7, int);
DEFN_SYSCALL1(malloc, void *, 8, size_t);
DEFN_SYSCALL1(free, int, 9, void *);
DEFN_SYSCALL2(stat, int, 10, const char *, struct stat *);
DEFN_SYSCALL1(chdir, int, 11, const char *);
DEFN_SYSCALL3(write, int, 12, int, const void *, int);
