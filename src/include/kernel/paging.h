#ifndef _PAGING_H
#define _PAGING_H

#include <types.h>
#include <kernel/interrupts.h>

#define PAGE_SIZE 0x1000

/* Represents a page entry in memory */
typedef struct page {
	uint32 present  : 1;  /* is page present in physical memory? */
	uint32 rw       : 1;  /* 0 if read-only, 1 if read-write */
	uint32 user     : 1;  /* 0 if kernel-mode, 1 if user-mode */
	uint32 pwt      : 1;  /* Page-level write-through */
	uint32 pcd      : 1;  /* Page-level cache disable */
	uint32 accessed : 1;  /* has the page been accessed (read or written) since last refresh? */
	uint32 dirty    : 1;  /* has the page been written to since last refresh? */
	uint32 pat      : 1;
	uint32 global   : 1;  /* if CR4.PGE = 1, determines whether the translation is global; ignored otherwise */
	uint32 guard	: 1; // One of the three "avail" bits; is this page a guard page?
	uint32 avail    : 2;  /* bits available for use by the OS; one is used by the OS */
	uint32 frame    : 20; /* high 20 bits of the frame address */
} __attribute__((packed)) page_t;

#define PAGE_PRESENT (1 << 0)
#define PAGE_RW (1 << 1)
#define PAGE_USER (1 << 2)
#define PAGE_PWT (1 << 3)
#define PAGE_PCD (1 << 4)
#define PAGE_ACCESSED (1 << 5)
#define PAGE_DIRTY (1 << 6)
#define PAGE_PAT (1 << 7)
#define PAGE_GLOBAL (1 << 8)
#define PAGE_GUARD (1 << 9)

/* Represents a page table in memory */
typedef struct page_table {
	page_t pages[1024];
} page_table_t;

/* Represents a page directory in memory */
typedef struct page_directory {
	/* Physical positions of the page tables */
	uint32 tables_physical[1024];

	/* An array of pointers to page tables */
	page_table_t *tables[1024];

	/* The physical address *of* the variable tables_physical */
	uint32 physical_address;
} page_directory_t;

extern page_directory_t *kernel_directory;
extern page_directory_t *current_directory;

// TODO: these should probably either be in their own file, or static/"private" (except pmm_bytes_free)
uint32 pmm_alloc(void);
uint32 pmm_alloc_continuous(uint32 num_frames);
void pmm_free(uint32 phys_addr);
uint32 pmm_bytes_free(void);

uint32 vmm_alloc_kernel(uint32 start_virtual, uint32 end_virtual, bool continuous_physical, bool writable);
void vmm_alloc_user(uint32 start_virtual, uint32 end_virtual, page_directory_t *dir, bool writable);
void vmm_map_kernel(uint32 virtual, uint32 physical, bool writable);
void vmm_unmap(uint32 virtual, page_directory_t *dir);
void vmm_free(uint32 virtual, page_directory_t *dir);
uint32 vmm_get_phys(uint32 virtual, page_directory_t *dir);
void vmm_set_guard(uint32 virtual, page_directory_t *dir, bool guard /* true to set, false to clear */);

/* Sets up everything required and activates paging. */
void init_paging(unsigned long upper_mem);

/* Loads the page directory at /new/ into the CR3 register. */
void switch_page_directory(page_directory_t *new);

/* The page fault interrupt handler. */
uint32 page_fault_handler(uint32);

/* Returns the number of bytes free in PHYSICAL memory. */
uint32 pmm_bytes_free(void);

page_directory_t *create_user_page_dir(void);
void destroy_user_page_dir(page_directory_t *dir);

#endif /* header guard */
