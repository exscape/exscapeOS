#include <kernel/elf.h>
#include <kernel/kernutil.h>
#include <kernel/console.h>
#include <kernel/backtrace.h>

extern struct symbol *syms;

// Translate a kernel EIP (e.g. 0x104e3c) to a function name
struct symbol *addr_to_func(uint32 addr) {
	if (addr <= 0x100000 || addr >= 0x130000)
		return NULL;
	struct symbol *symp = syms;
	struct symbol tmp = { .eip = 0xffffffff, .name = NULL };
	struct symbol *best_match = &tmp;

	uint32 min_diff = 0xffffffff;
	while (symp->eip) {
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

	// We didn't find anything if the best match hasn't been updated properly.
	// Either that, or we DID find something, but it has no name.
	if (best_match->eip == 0xffffffff || best_match->name == NULL || *(best_match->name) == 0)
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
		if (ebp == NULL || ebp > (uint32 *)0xcfff0000 || ebp < (uint32 *)0x100000) {
			//printk("breaking; ebp = %p\n", ebp);
			break;
		}

		struct symbol *sym = addr_to_func(*(ebp + 1));
		if (sym == NULL) {
			bt->eip[i] = *(ebp + 1);
		}
		else {
			if (strcmp(sym->name, "_exit") == 0 && *(ebp + 1) - sym->eip == 0) {
				bt->eip[i] = 0;
				//printk("[end of backtrace - task entry point was above]\n");
			}
			else {
				bt->eip[i] = *(ebp + 1);
				//printk("%s+0x%x\n", sym->name, *(ebp + 1) - sym->eip);
			}
		}
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
			printk("0x%08x in ??? %s\n", bt->eip[i], IS_USER_SPACE(bt->eip[i]) ? "(userspace)" : "");
		else {
			if (strcmp(sym->name, "_exit") == 0 && bt->eip[i] - sym->eip == 0)
				printk("[end of backtrace - task entry point was above]\n");
			else
				printk("0x%08x in %s+0x%x\n", bt->eip[i], sym->name, bt->eip[i] - sym->eip);
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
