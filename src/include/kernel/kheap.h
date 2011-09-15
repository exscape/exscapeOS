#include <types.h>
#include <kernel/ordered_array.h>

/************************
 **** HEAP FUNCTIONS ****
 ************************/

#define IS_PAGE_ALIGNED(x) ((x & 0xfffff000) == 0)

/* 65536 bytes */
#define HEAP_INDEX_SIZE 0x10000

#define AREA_USED 0
#define AREA_FREE 1

/* Describes a heap structure - only one is used in the entire kernel */
typedef struct {
	uint32 start_address;
	uint32 end_address;
	uint32 max_address;
	uint8 supervisor;
	uint8 readonly;
	
	ordered_array_t *free_index; /* An array of area_t pointers */
	ordered_array_t *used_index; /* An array of area_t pointers */
} heap_t;

/* Describes an area header; placed before every block (free or used) */
typedef struct {
	uint32 magic;
	uint32 size; /* includes the header and footer! */
	uint8 type; /* == AREA_USED (0) || AREA_FREE (1) */
} area_header_t;

/* Describes an area footer; placed after every block (free or used) */
typedef struct {
	uint32 magic;
	area_header_t *header;
} area_footer_t;

/* Describes an area (free or used; see header->type); used in the indexes */
typedef struct {
	area_header_t *header;
	area_footer_t *footer;
} area_t;

/* Set up the heap location, and start off with a 4 MiB heap */
#define KHEAP_START 0xc0000000
#define KHEAP_INITIAL_SIZE 0x400000

heap_t *create_heap(uint32 start_address, uint32 initial_size, uint32 max_size, uint8 supervisor, uint8 readonly);


/*********************************
 **** PRE-HEAP/HEAP FUNCTIONS ****
 *********************************/

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
