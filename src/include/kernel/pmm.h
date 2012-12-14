#ifndef _PMM_H
#define _PMM_H

#include <sys/types.h>

void pmm_init(uint32 mbd_mmap_addr, uint32 mbd_mmap_length, uint32 upper_mem);
uint32 pmm_alloc(void);
uint32 pmm_alloc_continuous(uint32 num_frames);
void pmm_free(uint32 phys_addr);
uint32 pmm_bytes_free(void);

#endif
