#ifndef _KHEAP_H
#define _KHEAP_H

#include <sys/types.h>
#include <kernel/mutex.h>
#include <kernel/ordered_array.h>
#include <kernel/vmm.h>

/************************
 **** HEAP FUNCTIONS ****
 ************************/

#define IS_PAGE_ALIGNED(x) ((  ((uint32)(x)) & 0x00000fff) == 0)
#define IS_DWORD_ALIGNED(x) (( ((uint32)(x)) & 3) == 0)

#define HEAP_MAGIC 0xabcdef12

#define AREA_USED 0
#define AREA_FREE 1

#define FOOTER_FROM_HEADER(__h) ((area_footer_t *)((uint32)__h + __h->size - sizeof(area_footer_t)))

/* Describes an area header; placed before every block (free or used) */
typedef struct {
	uint32 size; /* includes the header and footer! */
	uint8 type; /* == AREA_USED (0) || AREA_FREE (1) */
	uint32 magic;
} area_header_t;

/* Describes an area footer; placed after every block (free or used) */
typedef struct {
	uint32 magic;
	area_header_t *header;
} area_footer_t;

/* Describes a heap structure - only one is used in the entire kernel */
typedef struct {
	uint32 start_address;
	uint32 end_address;
	uint32 _min_address; // start address is for the storage; _min_address for the entire heap w/ indexes etc.
	uint32 max_address;
	uint8 supervisor;
	uint8 readonly;

	ordered_array_t free_index; /* Stores an array of area_header_t pointers */
	ordered_array_t used_index; /* Stores an array of area_header_t pointers */

	area_header_t *rightmost_area; /* A pointer to the rightmost area, free or used. Used in both alloc() and free(). */

	mutex_t *mutex;
} heap_t;

/* 65536 areas (per array; one for free areas, one for used areas) */
#define HEAP_INDEX_SIZE 0x10000

#define KHEAP_START 0xc0000000
#define KHEAP_MAX_ADDR 0xcffff000 /* one page less than 0xd000000 */

/* Set up the heap location, and start off with a 4 MiB heap */
//#define KHEAP_INITIAL_SIZE 0x400000 /* 4 MiB; KEEP IN MIND that this MUST be larger than 2 * sizeof(type_t) * HEAP_INDEX_SIZE! */
//#define HEAP_MIN_GROWTH 0x200000 /* 2 MiB; the smallest amount the heap is expanded by for each call to heap_expand() */
//#define HEAP_MAX_WASTE 0x400000 /* 4 MiB; the largest the rightmost area (if it's free) is allowed to be before the heap is contracted */

/* Values for heap debugging */
#define KHEAP_INITIAL_SIZE 0x90000 /* 64 kiB + 512k for the indexes */
#define KHEAP_MIN_GROWTH 0x8000 /* 32 kiB */
#define KHEAP_MAX_WASTE 0x120000 /* must be >1 MiB */

#define USER_HEAP_INITIAL_SIZE 0x90000
#define USER_HEAP_MIN_GROWTH 0x8000
#define USER_HEAP_MAX_WASTE 0x120000

#define USER_HEAP_START 0x20000000
#define USER_HEAP_MAX_ADDR 0xbff00000

void stop_leak_trace(void);
void start_leak_trace(void);

heap_t *heap_create(uint32 start_address, uint32 initial_size, uint32 max_address, uint8 supervisor, uint8 readonly, struct task_mm *mm);
void heap_destroy(heap_t *heap, page_directory_t *dir);
void *heap_alloc(uint32 size, bool page_align, heap_t *heap);
void heap_free(void *p, heap_t *heap);

void validate_heap_index(bool print_headers);
void print_heap_index(void);

void kfree(void *p);

uint32 kheap_used_bytes(void);

// user space/syscalls (until a proper user space heap w/ brk() and sbrk() exists)
void *malloc(size_t);
void free(void *);

/*********************************
 **** PRE-HEAP/HEAP FUNCTIONS ****
 *********************************/

/* Allocate /size/ bytes of memory.
 * If align == true, the return value will be page aligned.
 * If phys isn't NULL, it will be set to the physical address of the allocated area.
 */
void *kmalloc_int(uint32 size, bool align, uint32 *phys);

/*
 * Tests whether a pointer belongs to the kernel heap or not.
 * Useful for checking use-after-free bugs.
 */
bool kheap_is_valid(void *p);

/* Plain kmalloc; not page-aligned, doesn't return the physical address */
void *kmalloc(uint32 size);

/* Page-aligned kmalloc */
void *kmalloc_a(uint32 size);

/* Returns the physical address in phys, but doesn't page align. */
void *kmalloc_p(uint32 size, uint32 *phys);

/* Returns the physical address in phys, and page aligns. */
void *kmalloc_ap(uint32 size, uint32 *phys);

/* Expand the size of an existing allocation. */
void *krealloc(void *p, size_t new_size);

#endif
