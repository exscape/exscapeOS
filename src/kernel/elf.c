#include <kernel/kernutil.h>
#include <kernel/console.h>
#include <kernel/initrd.h>
#include <kernel/kheap.h>
#include <kernel/elf.h>
#include <string.h>
#include <kernel/paging.h>

#define ELF_DEBUG 0

void elf_load(fs_node_t *fs_node, uint32 file_size, page_directory_t *task_dir) {
	// Loads to a fixed address of 0x10000000 for now; not a HUGE deal
	// since each (user mode) task has its own address space

	unsigned char *data = kmalloc(file_size);
	assert(read_fs(fs_node, 0, file_size, data) == file_size);

	elf_header_t *header = (elf_header_t *)data;

	const unsigned char ELF_IDENT[] = {0x7f, 'E', 'L', 'F'};

	assert(memcmp(header->e_ident.ei_mag, &ELF_IDENT, 4) == 0);
	assert(header->e_ident.ei_class == ELFCLASS32);
	assert(header->e_ident.ei_data == ELFDATA2LSB);
	assert(header->e_ident.ei_version == 1);

	assert(header->e_machine == EM_386);
	assert(header->e_entry > 0);
	assert(header->e_type == ET_EXEC);

	bool reenable_interrupts = interrupts_enabled(); disable_interrupts();

	for (int i=0; i < header->e_phnum; i++) {
		Elf32_Phdr *phdr = (Elf32_Phdr *)(data + header->e_phoff + header->e_phentsize * i);
		if (phdr->p_type == PT_LOAD) {
			// This is a segment to load!

			// Should this be writable to the task?
			bool writable = ((phdr->p_flags & PF_W) ? true : false);

#if ELF_DEBUG
			printk("Segment #%u: copy %u bytes from 0x%08x (data + offset) to 0x%08x (virt in task page dir); read%s\n",
					i, phdr->p_filesz, data + phdr->p_offset, phdr->p_vaddr, writable ? "-write" : "only");
#endif

			if (i == 0)
				assert(phdr->p_vaddr == 0x10000000);
			else
				assert(phdr->p_vaddr > 0x10000000);

			uint32 start_addr = phdr->p_vaddr;
			uint32 end_addr   = start_addr + phdr->p_memsz;
			if (!IS_PAGE_ALIGNED(end_addr)) {
				end_addr &= 0xfffff000;
				end_addr += 0x1000;
			}

			//uint32 kern_start = 0x1000000; /* 16 MB */
			//uint32 kern_end = kern_start + (end_addr - start_addr);

			for (uint32 addr = start_addr; addr < end_addr; addr += 0x1000) {
				// Allocate a physical frame for this address in the task's address space, set for user mode
				alloc_frame(addr, task_dir, false, writable);

				assert(current_directory == kernel_directory);

				// Also map this in kernel space
				map_phys_to_virt(virtual_to_physical(addr, task_dir), addr, true, true);
			}

			// Okay, we should have the memory. Let's clear it (since PARTS may be left empty by the memcpy,
			// e.g. the .bss section, and we do want zeroes to be there)
			memset((void *)start_addr, 0, end_addr - start_addr);

			// Copy the segment (e.g. .text + .rodata + .eh_frame, or .data + .bss) to the location
			memcpy((void *)start_addr, data + phdr->p_offset, phdr->p_filesz);

			// Unmap the memory in the kernel directory...
			//for (uint32 addr = kern_start; addr < kern_end; addr += 0x1000) {
			//assert(current_directory == kernel_directory);
			//
			//unmap_virt(addr);
			//}
		}
		else
			printk("Warning: skipping unsupported ELF program header (#%u, p_type = 0x%x)\n", i, phdr->p_type);
	}

#if ELF_DEBUG

	printk("File has %u program headers (each %u bytes), %u section headers (each %u bytes)\n",
		   header->e_phnum, header->e_phentsize, header->e_shnum, header->e_shentsize);

	printk("Program Header:\n");
	for (int i=0; i < header->e_phnum; i++) {
		Elf32_Phdr *phdr = (Elf32_Phdr *)(data + header->e_phoff + header->e_phentsize * i);

		if (phdr->p_type == PT_LOAD) {
			printk("LOAD  offset 0x%08x vaddr 0x%08x alignment %u bytes\n", phdr->p_offset, phdr->p_vaddr, phdr->p_align);
			unsigned int f = phdr->p_flags;
			printk("      filesz 0x%08x memsz 0x%08x flags %c%c%c\n", phdr->p_filesz, phdr->p_memsz, 
					(f & PF_R ? 'r' : '-'), (f & PF_W ? 'w' : '-'), (f & PF_X ? 'x' : '-'));
		}
		else {
			printk("unsupported program header (#%u), skipping\n", i);
		}

	}

	// Find the string table
	Elf32_Shdr *string_table_hdr = (Elf32_Shdr *)(data + header->e_shoff + header->e_shentsize * header->e_shstrndx);
	char *string_table = (char *)(data + string_table_hdr->sh_offset);

	printk("Sections:\n");
	printk("Idx         Name Size     VMA      LMA      File off Align\n");
	for (int i=1; i < header->e_shnum; i++) { // skip #0, which is always empty
		Elf32_Shdr *shdr = (Elf32_Shdr *)(data + header->e_shoff + header->e_shentsize * i);
		shdr=shdr;

		char *name = (char *)&string_table[shdr->sh_name];

		printk("%03d %12s %08x %08x %08x %08x %u\n", i, name, shdr->sh_size, shdr->sh_addr, shdr->sh_addr /* TODO: LMA */, shdr->sh_offset, shdr->sh_addralign);
		unsigned int f = shdr->sh_flags;
		printk("                 ");
		if (shdr->sh_type != SHT_NOBITS)
			printk("CONTENTS, ");
		if ((f & SHF_ALLOC))
			printk("ALLOC, ");
		// TODO: LOAD
		if ((f & SHF_WRITE) == 0)
			printk("READONLY, ");
		if ((f & SHF_EXECINSTR))
			printk("CODE\n");
		else
			printk("DATA\n");
	}
	printk("done in elf_load\n");
#endif // ELF_DEBUG

	kfree(data);
	if (reenable_interrupts) enable_interrupts();
}
