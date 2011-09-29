#include <types.h>
#include <stdlib.h> /* itoa(), reverse() */
#include <string.h> /* memset(), strlen() */
#include <kernel/kernutil.h> /* inb, inw, outw */
#include <kernel/monitor.h> /* printing, scrolling etc. */
#include <kernel/gdt.h>
#include <kernel/interrupts.h>
#include <stdio.h>
#include <kernel/keyboard.h>
#include <kernel/timer.h>
#include <kernel/kheap.h>
#include <kernel/paging.h>
#include <kernel/rtc.h>
#include <kernel/multiboot.h>
#include <kernel/initrd.h>
#include <kernel/task.h>
#include <kernel/syscall.h>
#include <kernel/kshell.h>

/* kheap.c */
extern uint32 placement_address;

void kmain(multiboot_info_t *mbd, unsigned int magic) {
	if (magic != 0x2BADB002) {
		panic("Invalid magic received from bootloader!");
	}

	if (mbd->mods_count == 0) {
		panic("initrd.img not loaded! Make sure the GRUB config contains a \"module\" line.\nSystem halted.");
	}

	/* This must be done before anything below (GDTs, etc.), since kmalloc() may overwrite the initrd otherwise! */
	uint32 initrd_location = *((uint32 *)mbd->mods_addr);
	uint32 initrd_end = *((uint32 *)(mbd->mods_addr + 4));
	if (initrd_end > placement_address)
		placement_address = initrd_end;

	/* This should be done EARLY on, since many other things will fail (possibly even panic() output) otherwise. */
	init_video();

	if (mbd->flags & 1) {
		printk("Memory info (thanks, GRUB!): %u kiB lower, %u kiB upper\n",
			mbd->mem_lower, mbd->mem_upper);
	}
	else
		panic("mbd->flags bit 0 is unset!");

	/* Time to get started initializing things! */
	printk("Initializing GDTs... ");
	gdt_install();
	printk("done\n");

	/* Load the IDT */
	printk("Initializing IDTs... ");
	idt_install();
	printk("done\n");

	/* Enable interrupts */
	printk("Initializing ISRs and enabling interrupts... ");
	enable_interrupts();
	printk("done\n");

	/* Set up the keyboard callback */
	printk("Setting up the keyboard handler... ");
	init_keyboard();
	printk("done\n");

	/* Set up the PIT and start counting ticks */
	printk("Initializing the PIT... ");
	timer_install();
	printk("done\n");

	/* Initialize the initrd */
	/* (do this before paging, so that it doesn't end up in the kernel heap) */
	fs_root = init_initrd(initrd_location);

	/* Set up paging and the kernel heap */
	printk("Initializing paging and setting up the kernel heap... ");
	init_paging(mbd->mem_upper);
	printk("done\n");

	/* Set up the syscall interface */
	printk("Initializing syscalls... ");
	init_syscalls();
	printk("done\n");

	printk("All initialization complete!\n\n");

	//switch_to_user_mode();
	//asm volatile("hlt");

	//return;

	/*
	unsigned char ch;
	while (true) {
		ch = getchar();
		putchar(ch);

		if (ch == 0x08) {
			putchar(' '); cursor.x--;
		}

		update_cursor();
	}
	*/


	/* Start the kernel shell */
	kshell();

	printk("\n\n");
	printk("kmain() done; running infinite loop\n");

	Time t;
	memset(&t, 0, sizeof(t));
	get_time(&t);

	for (;;) {
		get_time(&t);
		print_time(&t);
		asm volatile("hlt"); /* Since the PIT fires every 10ms or so, sleep the CPU until then. QEMU CPU usage goes from 100% to 4% with this line. */
	}
}
