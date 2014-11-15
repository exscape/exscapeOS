#ifndef _FPU_H
#define _FPU_H

void fpu_init(void);

// Note: this struct must always be 16-byte aligned, e.g. using __attribute__((aligned(16))), or
// page-aligned kmalloc.
typedef struct fpu_mmx_state {
	char data[512];
} fpu_mmx_state_t;

#endif
