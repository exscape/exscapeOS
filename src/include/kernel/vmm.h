#ifndef _PAGING_H
#define _PAGING_H

#include <sys/types.h>
#include <kernel/interrupts.h>
#include <kernel/list.h>

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
	uint32 pat      : 1;  /* not used: related to caching in Pentium III and newer processors */
	uint32 global   : 1;  /* if CR4.PGE = 1, determines whether the translation is global; ignored otherwise */
	uint32 guard	: 1;  /* One of the three "avail" bits; is this page a guard page? */
	uint32 avail    : 2;  /* bits available for use by the OS; one of the three is used (above) */
	uint32 frame    : 20; /* high 20 bits of the frame address */
} __attribute__((packed)) page_t;

// Page Table Entry bits, i.e. ones used to control a single page
#define PTE_PRESENT (1 << 0)
#define PTE_RW (1 << 1)
#define PTE_USER (1 << 2)
#define PTE_PWT (1 << 3)
#define PTE_PCD (1 << 4)
#define PTE_ACCESSED (1 << 5)
#define PTE_DIRTY (1 << 6)
#define PTE_PAT (1 << 7)
#define PTE_GLOBAL (1 << 8)
#define PTE_GUARD (1 << 9)

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

// Defines that can be used as parameters to the below functions
#define PAGE_RO 0
#define PAGE_RW 1
#define PAGE_ANY_PHYS 0
#define PAGE_CONTINUOUS_PHYS 1

// Everything outside of the user mode address range of 0x1000000 to 0xc0000000 (exclusive) is kernel space.
// Currently, 0x10000000 is where ELF programs are loaded, while 0xbffff000 is the start of the userspace stack.
// The rest of the space (virtually that entire space except the few pages used by the program + data and
// the stack) is currently unused.
// (Keep in mind that the stack grows towards LOWER addresses).
size_t user_strlen(const char *);
#define IS_USER_SPACE(addr) ( (((uint32)addr) >= 0x10000000 && ((uint32)addr) < 0xc0000000) )
#define IS_KERNEL_SPACE(addr) ( !IS_USER_SPACE(addr) )
#define CHECK_ACCESS_READ(addr, len) ( IS_USER_SPACE(addr) && IS_USER_SPACE((uint32)addr + (uint32)len) )
bool vmm_check_access_write(uint32, uint32);
#define CHECK_ACCESS_WRITE(addr, len) ( CHECK_ACCESS_READ(addr, len) && vmm_check_access_write((uint32)addr, (uint32)len) )
#define CHECK_ACCESS_STR(s) ( CHECK_ACCESS_READ(s, user_strlen(s) + 1) )

// Describes the memory areas of a task; user mode only
struct task_mm {
	list_t *areas;
	uint32 text_start;
	uint32 text_end;
	uint32 brk_start;
	uint32 brk;
	uint32 initial_brk; // current brk when userspace takes over; only sbrk may change it after that, and never to a value below initial_brk
	page_directory_t *page_directory;
	uint32 user_stack_guard_page; // Lowest address of the user stack (the lowest usable is this + PAGE_SIZE)

	uint32 frames_used; // number of frames used by this task
};

typedef struct vm_area {
	void *start;
	void *end; // exclusive; a 1-page area starting at address 0x1000 has end == 0x2000
	bool writable;
} vm_area_t;

// Clones a userspace task's memory structures etc.
struct task_mm *vmm_clone_mm(struct task_mm *parent_mm);

// Allocate memory for kernel mode, with continuous or 'any' physical addresses, to the specified virtual addresses
uint32 vmm_alloc_kernel(uint32 start_virtual, uint32 end_virtual, bool continuous_physical, bool writable);

// Allocate memory for user mode, with any physical addresses, to the specified virtual addresses in the specified page directory
void vmm_alloc_user(uint32 start_virtual, uint32 end_virtual, struct task_mm *mm, bool writable);

// Free all memory allocated to a userspace task
void vmm_destroy_task_mm(struct task_mm *mm);

// Map a virtual address to a physical address, with no allocotion (e.g. for MMIO), with the page set te kernel mode
void vmm_map_kernel(uint32 virtual, uint32 physical, bool writable);

// Unmap a virtual address, without deallocating the physical frame (e.g. for unmapping MMIO addresses)
void vmm_unmap(uint32 virtual, page_directory_t *dir);

// Free and unmap a page allocation previously allocated with vmm_alloc_{kernel,user}. NOTE: frees ONE page only, not necessarily the entire set allocated!
void vmm_free(uint32 virtual, page_directory_t *dir);

// Calculate the physical address for a known virtual one
uint32 vmm_get_phys(uint32 virtual, page_directory_t *dir);

// Manage guard pages, i.e. pages with present = 0 to catch invalid reads/writes
void vmm_set_guard(uint32 virtual, page_directory_t *dir);
void vmm_clear_guard(uint32 virtual, page_directory_t *dir);

struct task_mm *vmm_create_user_mm(void);
struct task_mm *vmm_create_kernel_mm(void);

/* Sets up everything required and activates paging. */
void init_paging(unsigned long mbd_mmap_addr, unsigned long mbd_mmap_length, unsigned long upper_mem);

/* Loads the page directory at /new/ into the CR3 register. */
void switch_page_directory(page_directory_t *new);

/* The page fault interrupt handler. */
uint32 page_fault_handler(uint32);

// The sbrk syscall (increases heap area for user mode tasks) */
void *sbrk(sint32 incr);

page_directory_t *create_user_page_dir(void);
void destroy_user_page_dir(page_directory_t *dir);

#endif /* header guard */
