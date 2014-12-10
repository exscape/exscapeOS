#include <kernel/elf.h>
#include <kernel/kernutil.h>
#include <kernel/console.h>
#include <kernel/backtrace.h>

extern struct symbol *kernel_syms;

// Translate an EIP value (e.g. 0x104e3c) to a function name
struct symbol *addr_to_func(uint32 addr) {
	if (!(
		(addr >= 0x100000 && addr <= 0x200000) ||   // possibly valid kernel EIP
		(addr >= 0x10000000 && addr <= 0x20000000)  // possibly valid userspace EIP
		))
		return NULL;

	struct symbol tmp = { .eip = 0xffffffff, .name = NULL };
	struct symbol *best_match = &tmp;
	uint32 min_diff = 0xffffffff;

	struct symbol *all_syms[] = { kernel_syms, current_task->symbols, NULL };

	// So this is a bit messy, but here's the idea behind each symbol pointer, whether * or **
	// kernel_syms: pointer to an array of struct symbol entries
	// current_task->symbols: as above, for the current userspace task
	// all_syms: contains pointers to both of the above, to make looping easier. might be extended in the future.
	// symp (defined below): the current symbol we're testing
	// Note that we loop until eip == 0; the array is "null terminated", so to speak, with
	// a null strect entry (eip == 0, name == NULL).
	for (int symlist = 0; all_syms[symlist] != NULL; symlist++) {
		struct symbol *symp = all_syms[symlist];
		while (symp->eip && symp->name) {
			// Compare all symbols to the address, and see if there's a better match.
			// "Better match" is defined as the symbol starting BEFORE the address, such
			// that the address may be INSIDE the symbol function, *and* closer to it than
			// previously. We don't know the function length, so this will have to do.
			// So far, it's worked 100%.
			if (symp->eip <= addr && (addr - symp->eip) < min_diff) {
				best_match = symp;
				min_diff = addr - symp->eip;
			}
			symp++;
		}
	}

	// We didn't find anything if the best match hasn't been updated properly.
	// Either that, or we DID find something, but it has no name.
	//printk("best_match = 0x%p, name ADDRESS = 0x%p\n", best_match, &best_match->name);
	if (best_match->eip == 0xffffffff || best_match->name == NULL || *(best_match->name) == 0)
		return NULL;

	// This "best" match is so bad that it's certain to be incorrect
	if (addr - best_match->eip > 0x200000)
		return NULL;

	return best_match;
}

// Find a backtrace from the passed EBP value, and store it in bt.
// Depths higher than BACKTRACE_MAX are cut off.
// If eip[i] == 0, that signals the end of the backtrace.
void get_backtrace(uint32 _ebp, struct backtrace *bt) {
	memset(bt, 0, sizeof(struct backtrace));
	uint32 *ebp = (uint32 *)_ebp;
	int i = 0;
	while (ebp != NULL) {
		if (i >= BACKTRACE_MAX)
			break;
		if (ebp > (uint32 *)0xcfff0000 || ebp < (uint32 *)0x100000) {
			break;
		}

		bt->eip[i] = *(ebp + 1);
		ebp = (uint32 *)*ebp;
		i++;
	}
}

// Print a backtrace as obtained by the function above
void print_backtrace_struct(struct backtrace *bt) {
	assert(bt != NULL);
	for (int i = 0; i < BACKTRACE_MAX; i++) {
		if (bt->eip[i] == 0)
			break;

		struct symbol *sym = addr_to_func(bt->eip[i]);
		if (bt->eip[i] == 0xffffffff || sym == NULL)
			printk("0x%08x in ???%s\n", bt->eip[i], IS_USER_SPACE(bt->eip[i]) ? " (userspace)" : "");
		else if (sym != NULL) {
			printk("0x%08x in %s+0x%x%s\n", bt->eip[i], sym->name, bt->eip[i] - sym->eip, IS_USER_SPACE(bt->eip[i]) ? " (userspace)" : "");
		}
	}
}

// Combine the two above functions to make printing really simple
void print_backtrace_ebp(uint32 _ebp) {
	struct backtrace bt;
	get_backtrace(_ebp, &bt);
	print_backtrace_struct(&bt);
}

// ... and combine the THREE functions above to make printing "right here, right now" even simpler yet.
void print_backtrace(void) {
	uint32 ebp;
	asm volatile("mov %%ebp, %[ebp]" : [ebp]"=m"(ebp) : : "memory", "cc");
	print_backtrace_ebp(ebp);
}
