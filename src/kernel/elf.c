#include <kernel/kernutil.h>
#include <kernel/console.h>
#include <kernel/initrd.h>
#include <kernel/heap.h>
#include <kernel/elf.h>
#include <kernel/task.h>
#include <string.h>
#include <kernel/vmm.h>
#include <sys/errno.h>
#include <kernel/backtrace.h>

//#define ELF_DEBUG

// Kernel symbols are stored here; userspace symbols are linked in
// their task structs, so access them via current_task
struct symbol **kernel_syms = NULL;

int load_symbols(Elf32_Sym *symhdr, const char *sym_string_table, struct symbol ***syms, uint32 num_syms);

void load_kernel_symbols(void *addr, uint32 num, uint32 size, uint32 shndx) {
	// These cryptic parameters are given to us by GRUB/multiboot.
	// From testing, they appear to be (all e_* values are from the ELF header)
	// addr: address of section header table (NOT address of ELF header!),
	//       which is the ELF header's location + e_shoff
	// num: number of section headers, which is e_shnum
	// size: size of each entry, which is e_shentsize
	// shndx: the string table's index into the SHT, which is e_shstrndx

	const char *sym_string_table = NULL;

#if ELF_DEBUG
	char *string_table = (char *)(string_table_hdr->sh_addr);
	printk("Sections:\n");
	printk("Idx         Name Size     VMA      LMA      File off Align\n");
#endif

	Elf32_Sym *symhdr = NULL;
	uint32 num_syms = 0;

	for (uint32 i=1; i < num; i++) { // skip #0, which is always empty
		Elf32_Shdr *shdr = (Elf32_Shdr *)((uint32)addr + (size * i));

		if (shdr->sh_type == SHT_SYMTAB) {
			symhdr = (Elf32_Sym *)shdr->sh_addr;
			num_syms = shdr->sh_size / shdr->sh_entsize;
			Elf32_Shdr *string_table_hdr = (Elf32_Shdr *)((uint32)addr + shdr->sh_link * size);
			sym_string_table = (char *)(string_table_hdr->sh_addr);
			if (symhdr == NULL) {
				panic("Unable to locate kernel symbols!");
			}
#ifndef ELF_DEBUG
			break;
#endif
		}

#if ELF_DEBUG
		char *name = (char *)&string_table[shdr->sh_name];

		printk("%03d %12s %08x %08x %08x %08x %u\n", i, name, shdr->sh_size, shdr->sh_addr, shdr->sh_addr /* TODO: LMA */, shdr->sh_offset, shdr->sh_addralign);
		unsigned int f = shdr->sh_flags;
		printk("                 ");
		if (shdr->sh_type != SHT_NOBITS)
			printk("CONTENTS, ");
		if ((f & SHF_ALLOC))
			printk("ALLOC, ");
		if ((f & SHF_WRITE) == 0)
			printk("READONLY, ");
		if ((f & SHF_EXECINSTR))
			printk("CODE\n");
		else
			printk("DATA\n");
#endif
	}

	if (load_symbols(symhdr, sym_string_table, &kernel_syms, num_syms) != 0)
		panic("unable to load kernel symbols");
}

int load_symbols(Elf32_Sym *symhdr, const char *sym_string_table, struct symbol ***syms, uint32 num_syms) {
	assert(syms != NULL);
	assert(*syms == NULL);

	*syms = kmalloc(sizeof(struct symbol *) * (num_syms + 1));
	memset(*syms, 0, sizeof(struct symbol *) * (num_syms + 1));
	assert(*syms != NULL);
	for (uint32 i = 0; i < num_syms; i++) {
		assert(((*syms)[i] = kmalloc(sizeof(struct symbol))) != NULL);
	}

	struct symbol **symp = *syms;

	uint32 sym_num = 0;
	for (uint32 i = 1; i <= num_syms; i++) {
		symhdr++;

		if (ELF32_ST_TYPE(symhdr->st_info) != STT_FUNC)
			continue;

		const char *name;
		if (symhdr->st_name != 0)
			name = (char *)&sym_string_table[symhdr->st_name];
		else
			name = "N/A";
#if ELF_DEBUG
		printk("%03d 0x%08x %s\n", i, (uint32)symhdr->st_value, name);
#endif

		assert(symp != NULL);
		assert(symp[sym_num] != NULL);

		struct symbol *this_sym = symp[sym_num];
		this_sym->eip = symhdr->st_value;
		this_sym->name = strdup(name);
		sym_num++;
	}
	symp[sym_num] = NULL;

	return 0;
}

// Takes an array of argument (argv or envp) and copies it *FROM THE KERNEL HEAP*
// to userspace. Also frees them from the kernel heap, which is why that MUST be the source.
void copy_argv_env_to_task(char ***argv, uint32 argc, task_t *task) {
	assert(argv != NULL);
	assert(task != NULL);

	char **tmp_argv = *argv;

	size_t len = 0;
	for (size_t i = 0; tmp_argv[i] != NULL; i++) {
		len += sizeof(char *); // the argv pointer
		if (tmp_argv[i])
			len += strlen(tmp_argv[i]) + 1;
	}
	len += sizeof(char *); // the last NULL

	struct task_mm *mm = task->mm;
	assert(mm != NULL);
	assert(mm->brk_start != 0);

	size_t size = len;
	if (size & 0xfff) {
		size &= 0xfffff000;
		size += PAGE_SIZE;
	}

	vmm_alloc_user(mm->brk_start, mm->brk_start + size, mm, true /* read-write */);
	char *addr = (char *)mm->brk_start;
	mm->brk += size;
	mm->brk_start += size;

	memset(addr, 0, size);

	char **new_argv = (char **)addr;
	addr += (argc + 1) * sizeof(char *);

	// OK, we now have the space we need, time to actually start working.
	size_t i = 0;
	for (; tmp_argv[i] != NULL; i++) {
		size_t tlen = strlen(tmp_argv[i]) + 1;
		memcpy(addr, tmp_argv[i], tlen);
		new_argv[i] = addr;
		addr += tlen;
	}

	i = 0;
	while (tmp_argv[i] != NULL) {
		kfree(tmp_argv[i++]);
	}
	kfree(tmp_argv);

	*argv = new_argv;
}

static int elf_load_int(const char *path, task_t *task, char *argv[], char *envp[]);

bool elf_load(const char *path, task_t *task, void *cmdline) {
	// Pass command line arguments to the task
	// Parse the data into argv/argc
	uint32 argc = 0;
	char **argv = NULL;
	argv = parse_command_line(cmdline, &argc, task);
	assert(argv != NULL);
	assert(argv[0] != NULL);
	assert(strlen(argv[0]) > 0);

	char **envp = kmalloc(sizeof(char *));
	*envp = NULL;

	return elf_load_int(path, task, argv, envp) == 0;
}

static int elf_load_int(const char *path, task_t *task, char *argv[], char *envp[]) {
	// Loads to a fixed address of 0x10000000 for now; not a HUGE deal
	// since each (user mode) task has its own address space

	assert(interrupts_enabled() == false); // TODO: get rid of the race condition from create_task, so that this isn't needed

	assert(task != NULL);
	struct task_mm *mm = task->mm;
	assert(mm != NULL);

	struct stat st;

	int r;
	if ((r = stat(path, &st)) != 0) {
		assert(r < 0);
		return r;
	}

	uint32 file_size = st.st_size;
	unsigned char *data = kmalloc(file_size);
	int retval = 0;

	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		printk("elf_load(): unable to open %s\n", path);
		retval = fd;
		goto err;
	}

	if ((r = read(fd, data, file_size)) != (int)file_size) {
		printk("elf_load(): unable to read from %s; got %d bytes, requested %d\n", path, r, (int)file_size);
		if (r < 0) {
			retval = r;
			goto err;
		}
		else {
			panic("read() returned less than the expected file size, but not a negative value... why?");
			retval = -EIO;
			goto err;
		}
	}

	close(fd);

	elf_header_t *header = (elf_header_t *)data;

	const unsigned char ELF_IDENT[] = {0x7f, 'E', 'L', 'F'};

	if (memcmp(header->e_ident.ei_mag, &ELF_IDENT, 4) != 0) {
		printk("Warning: file %s is not an ELF file; aborting execution\n", path);
		retval = -ENOEXEC;
		goto err;
	}

	// TODO SECURITY: don't trust anything from the file - users can EASILY execute
	// "forged" ELF files!

	if (header->e_ident.ei_class != ELFCLASS32 || header->e_ident.ei_data != ELFDATA2LSB || \
		header->e_ident.ei_version != 1 || header->e_machine != EM_386 || header->e_type != ET_EXEC) {
		printk("Warning: file %s is not a valid ELF file (invalid ELFCLASS, ELFDATA, version, machine or not ET_EXEC\n");
		retval = -ENOEXEC;
		goto err;
	}

	assert(header->e_entry >= 0x10000000);
	assert(header->e_entry <  0x11000000);

	if (task == current_task) {
		// execve
		assert(current_task->mm != NULL);
		assert(current_task->mm->areas != NULL);
		assert(current_task->mm->page_directory != NULL);
	}

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
			uint32 start_addr_aligned = (phdr->p_vaddr & 0xfffff000);
			uint32 end_addr   = start_addr + phdr->p_memsz;
			if (!IS_PAGE_ALIGNED(end_addr)) {
				end_addr &= ~(PAGE_SIZE - 1);
				end_addr += PAGE_SIZE;
			}

			if (end_addr > task->mm->brk_start) {
				uint32 new_brk = end_addr;
				if (!IS_PAGE_ALIGNED(new_brk)) {
					new_brk &= ~(PAGE_SIZE - 1);
					new_brk += PAGE_SIZE;
				}
				task->mm->brk_start = new_brk;
				task->mm->brk = new_brk;
			}

			// Allocate memory for this address in the task's address space, set for user mode
			vmm_alloc_user(start_addr_aligned, end_addr, mm, writable);

			// Switch to the new page directory, so that we can copy the data there
			page_directory_t *old_dir = current_directory;
			switch_page_directory(mm->page_directory);

			// Okay, we should have the memory. Let's clear it (since PARTS may be left empty by the memcpy,
			// e.g. the .bss section, and we do want zeroes to be there)
			memset((void *)start_addr_aligned, 0, end_addr - start_addr_aligned);

			// Copy the segment (e.g. .text + .rodata + .eh_frame, or .data + .bss) to the location
			// DO NOT use start_addr_aligned here - we want the program to dictate the exact location

			memcpy((void *)start_addr, data + phdr->p_offset, phdr->p_filesz);

			switch_page_directory(old_dir);
		}
		else if (phdr->p_type == PT_GNU_STACK || phdr->p_type == PT_GNU_RELRO || phdr->p_type == PT_GNU_EH_FRAME) {
			// Quietly ignore
		}
		else
			printk("Warning: skipping unsupported ELF program header (#%u, p_type = 0x%x)\n", i, phdr->p_type);
	}

	// Set up the reentrancy structure for Newlib
	// (It is initialized below, after switching to the new page directory.)
	uint32 size = sizeof(struct _reent);
	if (size & 0xfff) {
		size &= 0xfffff000;
		size += PAGE_SIZE;
	}

	vmm_alloc_user(task->mm->brk, task->mm->brk + size, mm, true);

	//assert(current_directory == kernel_directory);
	page_directory_t *old_dir = current_directory;
	switch_page_directory(task->mm->page_directory);

	task->reent = (struct _reent *)task->mm->brk;
	_REENT_INIT_PTR(task->reent);
	task->mm->brk += size;
	task->mm->brk_start += size;

	assert(IS_PAGE_ALIGNED(task->mm->brk));
	assert(task->mm->brk == task->mm->brk_start);

	// The value brk has when the process starts;
	// userspace may not decrease the brk point below this address
	task->mm->initial_brk = task->mm->brk_start;

	// Copy the argv data from the kernel heap to the task's address space
	// This function updates argv to point to the new location.
	uint32 argc = 0;
	for (; argv[argc] != NULL; argc++) { }
	copy_argv_env_to_task(&argv, argc, task);

	uint32 envc = 0;
	assert(envp != NULL);
	for (; envp[envc] != NULL; envc++) { }
	copy_argv_env_to_task(&envp, envc, task);

	*((uint32 *)(USER_STACK_START - 0)) = (uint32)envp;
	*((uint32 *)(USER_STACK_START - 4)) = (uint32)argv;
	*((uint32 *)(USER_STACK_START - 8)) = (uint32)argc;

	// Update the task's name
	strlcpy((char *)task->name, argv[0], TASK_NAME_LEN);

	if (old_dir != kernel_directory) {
		// execve, stay with the new dir
	}
	else
		switch_page_directory(old_dir);

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
	assert(header->e_shoff != 0); // we need a section header
	Elf32_Shdr *string_table_hdr = (Elf32_Shdr *)(data + header->e_shoff + header->e_shentsize * header->e_shstrndx);
	char *string_table = (char *)(data + string_table_hdr->sh_offset);

	printk("Sections:\n");
	printk("Idx         Name Size     VMA      LMA      File off Align\n");
	for (int i=1; i < header->e_shnum; i++) { // skip #0, which is always empty
		Elf32_Shdr *shdr = (Elf32_Shdr *)(data + header->e_shoff + header->e_shentsize * i);

		char *name = (char *)&string_table[shdr->sh_name];

		printk("%03d %12s %08x %08x %08x %08x %u\n", i, name, shdr->sh_size, shdr->sh_addr, shdr->sh_addr /* TODO: LMA */, shdr->sh_offset, shdr->sh_addralign);
		unsigned int f = shdr->sh_flags;
		printk("                 ");
		if (shdr->sh_type != SHT_NOBITS)
			printk("CONTENTS, ");
		if ((f & SHF_ALLOC))
			printk("ALLOC, ");
		if ((f & SHF_WRITE) == 0)
			printk("READONLY, ");
		if ((f & SHF_EXECINSTR))
			printk("CODE\n");
		else
			printk("DATA\n");
	}
#endif // ELF_DEBUG

	// Try to find symbols, so we can get nice backtrace displays
	Elf32_Sym *symhdr = NULL;
	uint32 num_syms = 0;
	const char *sym_string_table = NULL;

	for (uint32 i=1; i < header->e_shnum; i++) { // skip #0, which is always empty
		Elf32_Shdr *shdr = (Elf32_Shdr *)((uint32)data + header->e_shoff + (header->e_shentsize * i));

		if (shdr->sh_type == SHT_SYMTAB) {
			symhdr = (Elf32_Sym *)(data + shdr->sh_offset);
			num_syms = shdr->sh_size / shdr->sh_entsize;
			Elf32_Shdr *string_table_hdr = (Elf32_Shdr *)((uint32)data + header->e_shoff + shdr->sh_link * header->e_shentsize);
			sym_string_table = (char *)(data + string_table_hdr->sh_offset);
			break;
		}
	}

	// Load symbols for this file, so that we can display them
	// in backtraces
	if (!symhdr || !sym_string_table || num_syms < 1 || load_symbols(symhdr, sym_string_table, &task->symbols, num_syms) != 0) {
		printk("Warning: failed to load symbols for %s\n", path);
	}

	// If we're still here: set the program entry point
	// (This updates the value on the stack in task.c)
	task->new_entry = (uint32)header->e_entry;
	set_entry_point((task_t *)task, task->new_entry);

	retval = 0;
	/* fall through on success */

err:
	kfree(data);
	assert(retval <= 0);
	return retval;
}

int execve(const char *path, char *argv[], char *envp[]) {
	INTERRUPT_LOCK;
	int r = elf_load_int(path, (task_t *)current_task, argv, envp);
	kfree((void *)path);
	// argv and envp are freed in elf_load_int
	argv = envp = NULL;

	if (r == 0) {
		assert(interrupts_enabled() == false);
		current_task->state = TASK_RUNNING;
		destroy_user_page_dir(current_task->old_mm->page_directory);
		vmm_destroy_task_mm(current_task->old_mm);
		current_task->old_mm = NULL;

		current_task->did_execve = true;
		current_task->esp = (uint32)current_task->stack - 84 + 12;
		// Overwrite the task's stack with new, zeroed values for registers etc.

		set_task_stack((task_t *)current_task, NULL, 0, 0);
		assert(current_task->new_entry > 0x100000);
		set_entry_point((task_t *)current_task, current_task->new_entry);

		assert(current_directory == current_task->mm->page_directory);
		//current_task->esp = ((uint32)((uint32)USER_STACK_START - sizeof(registers_t)));

		INTERRUPT_UNLOCK;
		YIELD;
		panic("returned past execve()!");
	}
	else {
		assert(r < 0);

		// execve failed! Let's undo most of the work, and return.
		struct task_mm *new_mm = current_task->mm;
		current_task->mm = current_task->old_mm;
		switch_page_directory(current_task->mm->page_directory);

		destroy_user_page_dir(new_mm->page_directory);
		vmm_destroy_task_mm(new_mm);

		INTERRUPT_UNLOCK;
		return r;
	}

	return 0; // To silence warnings
}

int sys_execve(const char *path, char *argv[], char *envp[]) {
	if (path == NULL || !CHECK_ACCESS_STR(path))
		return -EFAULT;

	// Newlib calls execve() with invalid paths while attempting to find
	// the correct one from $PATH; check whether the path exists early on,
	// so that we don't waste time copying arguments and allocating memory
	// if we're going to fail anyway.
	struct stat st;
	if (stat(path, &st) != 0)
		return -ENOENT;

	if (argv == NULL || !CHECK_ACCESS_READ(argv, sizeof(char *)))
		return -EFAULT;
	if (envp != NULL && !CHECK_ACCESS_READ(envp, sizeof(char *)))
		return -EFAULT;

	// Unfortunately for us, all the arguments are stored in userspace.
	// That would be fine, if not for the fact that we are about to free
	// all that memory, in preparation for replacing this task!
	// We copy it to the kernel heap temporarily, and if that works out,
	// call execve() which requires kernelspace arguments.
	uint32 argc = 0, envc = 0;

	// First, check that all memory is valid; both the pointers themselves,
	// and the actual string data they point to.
	for (;; argc++) {
		if (!CHECK_ACCESS_READ(&argv[argc], sizeof(char *)))
			return -EFAULT;
		if (argv[argc] != NULL && !CHECK_ACCESS_STR(argv[argc]))
			return -EFAULT;
		else if (argv[argc] == NULL)
			break;
	}

	// If the caller (the user, prior to the Newlib glue) passes env == NULL,
	// syscalls.c will provide us with "environ" to copy, instead.
	// Thus, envp should never be NULL, unless:
	// 1) A Newlib bug exists, or
	// 2) The user explicitly bypasses Newlib and uses the syscall,
	//    in which case he'll have to take care of this.
	assert(envp != NULL);
	for (;; envc++) {
		if (!CHECK_ACCESS_READ(&envp[envc], sizeof(char *)))
			return -EFAULT;
		if (envp[envc] != NULL && !CHECK_ACCESS_STR(envp[envc]))
			return -EFAULT;
		else if (envp[envc] == NULL)
			break;
	}

	// OK, it all seems valid. Nice. We now also know the number of entries
	// in each array, which makes the copying process easier: we can allocate
	// memory up-front without any risk of wasting memory or needing to realloc().
	char **kargv = kmalloc(sizeof(char *) * (argc + 1));
	memset(kargv, 0, sizeof(char *) * (argc + 1));

	for (uint32 i = 0; i < argc; i++) {
		uint32 len = user_strlen(argv[i]) + 1;
		kargv[i] = kmalloc(len);
		strlcpy(kargv[i], argv[i], len);
	}
	assert(kargv[argc] == NULL);

	char **kenvp = kmalloc(sizeof(char *) * (envc + 1));
	memset(kenvp, 0, sizeof(char *) * (envc + 1));

	for (uint32 i = 0; i < envc; i++) {
		uint32 len = user_strlen(envp[i]) + 1;
		kenvp[i] = kmalloc(len);
		strlcpy(kenvp[i], envp[i], len);
	}
	assert(kenvp[envc] == NULL);

	// And, finally, copy the path.
	size_t len = strlen(path);
	char *kpath = kmalloc(len + 1);
	strlcpy(kpath, path, len + 1);

	current_task->old_mm = current_task->mm; // destroyed later on, if execve doesn't fail
	current_task->mm = vmm_create_user_mm();

	int r = execve(kpath, kargv, kenvp);
	// We never get here unless execve failed, as the new process image takes over
	printk("WARNING: execve failed with return value %d\n", r);
	return r;
}
