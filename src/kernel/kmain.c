#include <types.h>
#include <stdlib.h> /* itoa(), reverse() */
#include <string.h> /* memset(), strlen() */
#include <kernel/kernutil.h> /* inb, inw, outw */
#include <kernel/console.h> /* printing, scrolling etc. */
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
#include <kernel/ata.h>

/* kheap.c */
extern uint32 placement_address;

/* console.c */
extern const uint16 blank;

void test_task(void) {
	for (;;) {
		printk("In test_task()\n");
		sleep(1000);
		//loopsleep();
	}
}

void idle_task(void) {
	for (;;) {
		asm volatile("hlt");
	}
}

void force_switch_task(void) {
	for (;;) {
		asm volatile("int $0x7e");
	}
}

void kmain(multiboot_info_t *mbd, unsigned int magic, uint32 init_esp0) {

	/* Set up the kernel console keybuffer, to prevent panics on keyboard input.
	 * The kernel console isn't dynamically allocated, so this can be done
	 * this early without problems. */
	kernel_console.keybuffer.read_ptr = kernel_console.keybuffer.data;
	kernel_console.keybuffer.write_ptr = kernel_console.keybuffer.data;
	kernel_console.keybuffer.counter = 0;

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

	printk("Initializing multitasking and setting up the kernel task... ");
	init_tasking(init_esp0);
	printk("done\n");

	//printk("Starting idle_task... ");
	//create_task(idle_task, "idle_task", /*console = */ false);
	//printk("done\n");

	create_task(force_switch_task, "force_switch_task", false);

#if 1
	printk("Detecting ATA devices and initializing them... ");
	printk("\n");
	ata_init();
	printk("done\n");

	unsigned char *buf = kmalloc(512);
	ata_read(&devices[0], 0, buf);
	printk("Buffer contents LBA0: \"%s\"\n", (char *)buf);

	ata_read(&devices[0], 2, buf);
	printk("Buffer contents LBA1: \"%s\"\n", (char *)buf);
#endif

	uint32 start_t = gettickcount();
	for (uint64 i = 0; i < 64000; i++) {
		assert(buf != NULL);
		ata_read(&devices[0], i, buf);
		//assert(buf != NULL);
		//if (*buf == 0)
			//continue;
		//printk("LBA%u: \"%s\"\n", i, (char *)buf);
		//assert(buf != NULL);
	}
	uint32 end_t = gettickcount();
	uint32 d = end_t - start_t;
	d *= 10;
	printk("Reading 64000 sectors took %u ms\n", d);

	printk("All initialization complete!\n\n");

#if 1
	/* Set up the virtual consoles (Alt+F1 through F4 at the time of writing) */
	for (int i=0; i < NUM_VIRTUAL_CONSOLES; i++) {
		console_init(&virtual_consoles[i]);
		node_t *new_node = list_append(virtual_consoles[i].tasks, create_task(&kshell, "kshell", true));
		((task_t *)new_node->data)->console = &virtual_consoles[i];
		virtual_consoles[i].active = false;
	}

	console_switch(&virtual_consoles[0]);
#endif

	while (true) {
		//asm volatile("sti; hlt");
		asm volatile("int $0x7e");
	}

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

	printk("\n\n");
	printk("kmain() done; running infinite loop\n");
	for(;;);

	/*
	Time t;
	memset(&t, 0, sizeof(t));
	get_time(&t);

	for (;;) {
		get_time(&t);
		print_time(&t);
		asm volatile("int $0x7e");
	}
	*/
}
