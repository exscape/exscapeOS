#include <types.h>
#include <kernel/ordered_array.h>

/************************
 **** HEAP FUNCTIONS ****
 ************************/

#define KHEAP_START 0xC0000000
#define KHEAP_INITIAL_SIZE 0x100000
#define HEAP_INDEX_SIZE 0x20000
#define HEAP_MAGIC 0x123890AB
#define HEAP_MIN_SIZE 0x70000

typedef struct {
	uint32 magic;
	uint8 is_hole;
	uint32 size; /* block size, including the footer */
} header_t;

typedef struct {
	uint32 magic;
	header_t *header;
} footer_t;

typedef struct {
	ordered_array_t index;
	uint32 start_address;
	uint32 end_address;
	uint32 max_address;
	uint8 supervisor; /* should extra pages requested by us be mapped as supervisor-only? */
	uint8 readonly;   /* should extra pages requested by us be mapped as read-only? */
} heap_t;

heap_t *create_heap(uint32 start, uint32 end, uint32 max, uint8 supervisor, uint8 readonly);

/* Allocates a continuous region of memory /size/ in size. If page_align == 1, the block itself starts on a page boundary. */
void *alloc(uint32 size, uint8 page_align, heap_t *heap);
void free(void *p, heap_t *heap);

/****************************
 **** PRE-HEAP FUNCTIONS ****
 ****************************/

/* Allocate /size/ bytes of memory.
 * If align == true, the return value will be page aligned.
 * If phys isn't NULL, it will be set to the physical address of the allocated area.
 */
uint32 kmalloc_int(uint32 size, bool align, uint32 *phys);

/* Plain kmalloc; not page-aligned, doesn't return the physical address */
uint32 kmalloc(uint32 size);

/* Page-aligned kmalloc */
uint32 kmalloc_a(uint32 size);

/* Returns the physical address in phys, but doesn't page align. */
uint32 kmalloc_p(uint32 size, uint32 *phys);

/* Returns the physical address in phys, and page aligns. */
uint32 kmalloc_ap(uint32 size, uint32 *phys);

/* Frees memory allocated by alloc(), which is used by kmalloc() after the heap is set up */
void kfree(void *p);
