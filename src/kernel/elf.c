#include <kernel/kernutil.h>
#include <kernel/console.h>
#include <kernel/initrd.h>
#include <kernel/heap.h>
#include <kernel/elf.h>
#include <kernel/task.h>
#include <string.h>
#include <kernel/vmm.h>

#define ELF_DEBUG 0

bool elf_load(const char *path, task_t *task) {
	// Loads to a fixed address of 0x10000000 for now; not a HUGE deal
	// since each (user mode) task has its own address space

	assert(interrupts_enabled() == false); // TODO: get rid of the race condition from create_task, so that this isn't needed

	page_directory_t *task_dir = task->page_directory;

	struct stat st;

	if (stat(path, &st) != 0) {
		printk("Unable to stat %s; halting execution\n", path);
		return false;
	}
	uint32 file_size = st.st_size;
	unsigned char *data = kmalloc(file_size);

	int fd = open(path, O_RDONLY);
	assert(fd >= 0);
	assert(read(fd, data, file_size) /* == file_size */); // TODO: check proper read!

	close(fd);

	elf_header_t *header = (elf_header_t *)data;

	const unsigned char ELF_IDENT[] = {0x7f, 'E', 'L', 'F'};

	if (memcmp(header->e_ident.ei_mag, &ELF_IDENT, 4) != 0) {
		printk("Warning: file %s is not an ELF file; aborting execution\n", path);
		kfree(data);
		return false;
	}

	assert(header->e_ident.ei_class == ELFCLASS32);
	assert(header->e_ident.ei_data == ELFDATA2LSB);
	assert(header->e_ident.ei_version == 1);

	assert(header->e_machine == EM_386);
	assert(header->e_entry > 0);
	assert(header->e_type == ET_EXEC);

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
				end_addr &= ~(PAGE_SIZE - 1);
				end_addr += PAGE_SIZE;
			}

			// Allocate memory for this address in the task's address space, set for user mode
			vmm_alloc_user(start_addr, end_addr, task_dir, writable);

			// Keep track of the allocated frames, so that we can free them when the task exits
			addr_entry_t *entry = kmalloc(sizeof(addr_entry_t));
			entry->start = (void *)start_addr;
			entry->num_pages = (end_addr - start_addr) / PAGE_SIZE;

			assert(task->mm != NULL);
			assert(task->mm->pages != NULL);

			list_append(task->mm->pages, entry);

			// Switch to the new page directory, so that we can copy the data there
			assert(current_directory == kernel_directory);
			switch_page_directory(task_dir);

			// Okay, we should have the memory. Let's clear it (since PARTS may be left empty by the memcpy,
			// e.g. the .bss section, and we do want zeroes to be there)
			memset((void *)start_addr, 0, end_addr - start_addr);

			// Copy the segment (e.g. .text + .rodata + .eh_frame, or .data + .bss) to the location
			memcpy((void *)start_addr, data + phdr->p_offset, phdr->p_filesz);

			// Return to the kernel's directory again
			switch_page_directory(kernel_directory);
		}
		else if (phdr->p_type == PT_GNU_STACK || phdr->p_type == PT_GNU_RELRO || phdr->p_type == PT_GNU_EH_FRAME) {
			// Quietly ignore
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
	return true;
}
