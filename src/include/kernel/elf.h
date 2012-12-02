#ifndef _ELF_H
#define _ELF_H

#include <string.h>
#include <kernel/vfs.h>
#include <kernel/task.h>

void elf_load(fs_node_t *fs_node, uint32 file_size, task_t *task);

typedef uint32 Elf32_Addr;
typedef uint16 Elf32_Half;
typedef uint32 Elf32_Off;
typedef sint32 Elf32_Sword;
typedef uint32 Elf32_Word;

typedef struct {
	uint8 ei_mag[4];  // {0x7f, 'E', 'L', 'F'
	uint8 ei_class;   // hopefully ELFCLASS32
	uint8 ei_data;    // hopefully ELFDATA2LSB
	uint8 ei_version; // always 1
	uint8 ei_pad[9];  // always 0
} elf_ident_header_t;

typedef struct {
	elf_ident_header_t e_ident;
	Elf32_Half		e_type;		/* object type, i.e. shared object, executable */
	Elf32_Half		e_machine;	/* machine, i.e. always 3 == i386 */
	Elf32_Word		e_version;	/* always 1 */
	Elf32_Addr		e_entry;	/* entry point virtual address */
	Elf32_Off		e_phoff;	/* program header table offset, or zero */
	Elf32_Off		e_shoff;	/* section header table offset, or zero */
	Elf32_Word		e_flags;	/* processor-specific flags */
	Elf32_Half		e_ehsize;	/* ELF header size in bytes */
	Elf32_Half		e_phentsize;/* size for each program header entry, in bytes */
	Elf32_Half		e_phnum;	/* number of program header entries (e_phentsize * e_phnum == sizeof(table) */
	Elf32_Half		e_shentsize;/* size of section header entry, in bytes */
	Elf32_Half		e_shnum;    /* number of section header entries (e_shentsize * e_shnum == sizeof(table) */
	Elf32_Half		e_shstrndx; /* name string table index, or SHN_UNDEF */
} elf_header_t;

/* e_type fields */
#define ET_NONE		0 /* no file type */
#define ET_REL		1 /* relocatable file */
#define ET_EXEC		2 /* executable file */
#define ET_DYN		3 /* shared object file */
#define ET_CORE		4 /* core file */
#define ET_LOPROC 0xff00 /* processor-specific */
#define ET_HIPROC 0xffff /* processor-specific */

/* e_machine fields */
#define EM_NONE		0 /* no machine */
#define EM_M32		1 /* AT&T WE 32100 */
#define EM_SPACE	2 /* SPARC */
#define EM_386		3 /* Intel 80386 */
#define EM_68K		4 /* Motorola 68000 */
#define EM_88K		5 /* Motorola 88000 */
#define EM_860		7 /* Intel 80860 */
#define EM_MIPS		8 /* MIPS RS3000 */

/* e_version fields */
#define EV_NONE 	0 /* invalid version */
#define EV_CURRENT	1 /* current version */

/* class and data types, in e_ident */
#define ELFCLASSNONE 0
#define ELFCLASS32	 1
#define ELFCLASS64	 2
#define ELFDATANONE  0
#define ELFDATA2LSB	 1
#define ELFDATA2MSB	 2

/* section indexes */
#define SHN_UNDEF		0	   /* undefined, missing, irrelevant section. */
#define SHN_LORESERVE	0xff00 /* lower bound of reserved indexes */
#define SHN_LOPROC		0xff00 /* lower bound for processor-specific indexes */
#define SHN_HIPROC		0xff1f /* upper bound for processor-specific indexes (inclusive range) */
#define SHN_ABS			0xfff1 /* symbols define "relative" to SHN_ABS are absolute valuess */
#define SHN_COMMON		0xfff2 /* common symbols, e.g. unallocated external C variables */
#define SHN_HIRESERVE	0xffff /* upper bound of reserved indexes */

typedef struct {
	Elf32_Word	sh_name;	 /* section name; index into section header string table */
	Elf32_Word	sh_type;	 /* defines the section's contents and semantics */
	Elf32_Word	sh_flags;	 /* Miscellaneous (1-bit) attributes */
	Elf32_Addr 	sh_addr;	 /* if this is section is part of the process, memory image: address to the section's first byte; else zero */
	Elf32_Off	sh_offset;	 /* offset from beginning of file to the first byte in the section. SHT_NOBITS notes *conceptual* placement */
	Elf32_Word	sh_size;	 /* section's size in bytes; if type is SHT_NOBITS, this can be nonzero despite using 0 bytes in the actual file */
	Elf32_Word	sh_link;	 /* section header index link; meaning depends on the section type */
	Elf32_Word	sh_info;	 /* extra information; meaning depends on the section type */
	Elf32_Word	sh_addralign;/* section's alignment. 0 or 1 means no alignment; otherwise powers of 2 are allowed. */
	Elf32_Word	sh_entsize;	 /* size of each entry, in bytes; zero if entries are not fixed size */
} Elf32_Shdr; // ELF section header

/* sh_type values */
#define SHT_NULL		0  /* inactive; no associated section */
#define SHT_PROGBITS	1  /* format decided solely by the program */
#define SHT_SYMTAB		2  /* complete symbol table */
#define SHT_STRTAB		3  /* string table */
#define SHT_RELA		4  /* Relocation entry with explicit addend, e.g. Elf32_Rela */
#define SHT_HASH		5  /* symbol hash table */
#define SHT_DYNAMIC		6  /* information for dynamic linking */
#define SHT_NOTE		7  /* note section */
#define SHT_NOBITS		8  /* occupies no space in the file, otherwise resembles SHT_PROGBITS */
#define SHT_REL			9  /* relocation entries without explicit addends, e.g. Elf32_Rel */
#define SHT_SHLIB		10 /* reserved */
#define SHT_DYNSYM		11 /* dynamic symbol table */
#define SHT_LOPROC		0x70000000 /* lower bound for processor-specific semantics */
#define SHT_HIPROC		0x7fffffff /* upper bound for processor-specific semantics */
#define SHT_LOUSER		0x80000000 /* lower bound of indexes reserved for application program */
#define SHT_HIUSER		0xffffffff /* upper bound of indexes reserved for application program */

/* sh_flags values */
#define SHF_WRITE	  0x1 /* section's contents should be writable during execution */
#define SHF_ALLOC	  0x2 /* section occupies memory during execution (off if section doesn't reside in memory) */
#define SHF_EXECINSTR 0x4 /* section contains executable machine instructions */
#define SHF_MASKPROC  0xf0000000 /* all bits in this mask are reserved for processor-specific semantics */

/* Symbol table */
typedef struct {
	Elf32_Word	st_name;  /* index into the symbol string table */
	Elf32_Addr	st_value; /* value of the symbol; meaning depends on the context */
	Elf32_Word	st_size;  /* object's size, in bytes; zero if unknown (or if size is indeed zero) */
	uint8		st_info;  /* symbol's type and binding attributes; see below */
	uint8		st_other; /* always 0 */
	Elf32_Half	st_shndx; /* section header table index for this symbol table entry */
} Elf32_Sym;

// macros for the st_info member
#define ELF32_ST_BIND(i) ((i)>>4)
#define ELF32_ST_TYPE(i) ((i)&0xf)
#define ELF32_ST_INFO(b,t) (((b)<<4)+((t)&0xf))

// Symbol binding, ELF32_ST_BIND(Elf32_Sym.st_info)
#define STB_LOCAL	0  /* local symbols, not visible outside the object file containing their definition */
#define STB_GLOBAL	1  /* global symbols */
#define STB_WEAK	2  /* weak symbols; resembles global symbols, but weak symbols have lower precedence */
#define STB_LOPROC	13 /* 13, 14, 15 are reserved for processor-specific semantics */
#define STB_HIPROC	15 /* ... */

// Symbol type, ELF_ST_TYPE(Elf32_Sym.st_info)
#define STT_NOTYPE	0  /* type not specified */
#define STT_OBJECT	1  /* symbol is associated with a data object (e.g. variable, array) */
#define STT_FUNC	2  /* symbol is associated with a function or other executable code */
#define STT_SECTION	3  /* symbol is associated with a section */
#define STT_FILE	4
#define STT_LOPROC	13 /* 13, 14, 15 are reserved for processor-specific semantics */
#define STT_HIPROC	15 /* ... */

// Program header
typedef struct {
	Elf32_Word	p_type;   /* the segment type; the meaning of the other values depends upon this */
	Elf32_Off	p_offset; /* offset from the beginning of the file to the first byte of this segment */
	Elf32_Addr	p_vaddr;  /* virtual address to the first byte of the array segment, in memory */
	Elf32_Addr	p_paddr;  /* physical variant of the above. Ignored for exscapeOS */
	Elf32_Word	p_filesz; /* number of bytes in the file image of the segment; may be zero */
	Elf32_Word	p_memsz;  /* number of bytes in the memory image of the segment; may be zero (and may be != p_filesz!) */
	Elf32_Word	p_flags;  /* various 1-bit flags, see below */
	Elf32_Word	p_align;  /* alignment for this segment in memory, e.g. 0 or 1 for no alignment, otherwise 2^n for int(n) */
} Elf32_Phdr;

// Segment types (Elf32_Phdr.p_type)
#define PT_NULL		0 /* unused element; other members' values are undefined. */
#define PT_LOAD		1 /* element describes a loadable segment. if p_memsz > p_filesz the extra bytes are 0. */
#define PT_DYNAMIC	2 /* element specifies dynamic linking information */
#define PT_INTERP	3 /* element specifes the location and size of a null-terminated path name to invoke as an interpreter */
#define PT_NOTE		4 /* element specifies the location and size of auxiliary information */
#define PT_SHLIB	5 /* element is reserved, with unspecified semantics */
#define PT_PHDR		6 /* element, if present, specifies the location and size of the program header table itself, in file and memory */
#define PT_LOPROC	0x70000000 /* this range, inclusive, is reserved for processor-specific semantics. */
#define PT_HIPROC	0x7fffffff /* ... see above */

// GNU extensions
#define PT_GNU_EH_FRAME 0x6474e550
#define PT_GNU_STACK	0x6474e551
#define PT_GNU_RELRO	0x6474e552

// Elf32_Phdr.p_flags
#define PF_R 0x4
#define PF_W 0x2
#define PF_X 0x1

#endif
