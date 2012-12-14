#include <sys/types.h>
#include <stdlib.h> /* itoa(), reverse() */
#include <string.h> /* memset(), strlen() */
#include <kernel/kernutil.h> /* inb, inw, outw */
#include <kernel/console.h> /* printing, scrolling etc. */
#include <kernel/gdt.h>
#include <kernel/interrupts.h>
#include <stdio.h>
#include <kernel/keyboard.h>
#include <kernel/timer.h>
#include <kernel/heap.h>
#include <kernel/vmm.h>
#include <kernel/rtc.h>
#include <kernel/multiboot.h>
#include <kernel/initrd.h>
#include <kernel/task.h>
#include <kernel/syscall.h>
#include <kernel/kshell.h>
#include <kernel/ata.h>
#include <kernel/partition.h>
#include <kernel/fat.h>
#include <kernel/pci.h>
#include <kernel/net/rtl8139.h>
#include <kernel/net/nethandler.h>
#include <kernel/net/arp.h>
#include <kernel/net/ipicmp.h>
#include <kernel/serial.h>

/* kheap.c */
extern uint32 placement_address;

/* console.c */
extern const uint16 blank;

/* fat.c */
extern list_t *fat32_partitions;

extern nethandler_t *nethandler_arp;
extern nethandler_t *nethandler_icmp;

//void idle_task(void) {
	//for (;;) {
		//asm volatile("hlt");
	//}
//}

extern volatile list_t ready_queue;

//char *kernel_cmdline = NULL;
//char *rootdev = NULL;

extern heap_t *kheap;

void cleanup_tasks(void *data, uint32 length) {
	while(true) {
		INTERRUPT_LOCK;
		for (node_t *it = ready_queue.head; it != NULL; it = it->next) {
			assert((uint32)it < (uint32)kheap->end_address);
			task_t *p = (task_t *)it->data;
			assert((uint32)p < (uint32)kheap->end_address);
			if (p->state == TASK_EXITING) {
				destroy_task(p);
			}
		}
		INTERRUPT_UNLOCK;
		sleep(10);
		//YIELD;
	}
}

extern uint32 end; // defined in linker.ld

void kmain(multiboot_info_t *mbd, unsigned int magic, uint32 init_esp0) {

	placement_address = (uint32)&end;

	/* This must be done before anything below (GDTs, etc.), since kmalloc() may overwrite the initrd otherwise! */
	uint32 initrd_location = *((uint32 *)mbd->mods_addr);
	uint32 initrd_end = *((uint32 *)(mbd->mods_addr + 4));
	if (initrd_end > placement_address)
		placement_address = initrd_end;

	kernel_console.tasks->mutex = mutex_create();
	ready_queue.mutex = mutex_create();

	/* Set up the kernel console keybuffer, to prevent panics on keyboard input.
	 * The kernel console isn't dynamically allocated, so this can be done
	 * this early without problems. */
	kernel_console.keybuffer.read_ptr = kernel_console.keybuffer.data;
	kernel_console.keybuffer.write_ptr = kernel_console.keybuffer.data;
	kernel_console.keybuffer.counter = 0;

	/* Set up the scrollback buffer for the kernel console */
	kernel_console.buffer = kmalloc(CONSOLE_BUFFER_SIZE_BYTES);
	kernel_console.bufferptr = kernel_console.buffer;
	kernel_console.current_position = 0;
	memsetw(kernel_console.buffer, (0x7 << 8 /* grey on black */) | 0x20 /* space */, CONSOLE_BUFFER_SIZE);

	/* Set up kernel console colors */
	kernel_console.text_color = LIGHT_GREY;
	kernel_console.back_color = BLACK;

	/* This should be done EARLY on, since many other things will fail (possibly even panic() output) otherwise.
	 * NOT earlier than the kernel console setup, though! */
	init_video();

	if (magic != 0x2BADB002) {
		panic("Invalid magic received from bootloader!");
	}

	if (mbd->mods_count == 0) {
		panic("initrd.img not loaded! Make sure the GRUB config contains a \"module\" line.\nSystem halted.");
	}

	printc(BLACK, RED, "exscapeOS starting up...\n");
	if (mbd->flags & 1) {
		printk("Memory info (thanks, GRUB!): %u kiB lower, %u kiB upper\n", mbd->mem_lower, mbd->mem_upper);
	}
	else
		panic("mbd->flags bit 0 is unset!");

	printk("Initializing serial port... ");
	init_serial();
	printc(BLACK, GREEN, "done\n");

	serial_send("Hello world!\n");

	/* Time to get started initializing things! */
	printk("Initializing GDTs... ");
	gdt_install();
	printc(BLACK, GREEN, "done\n");

	/* Load the IDT */
	printk("Initializing IDTs... ");
	idt_install();
	printc(BLACK, GREEN, "done\n");

	/* Enable interrupts */
	printk("Initializing ISRs and enabling interrupts... ");
	enable_interrupts();
	printc(BLACK, GREEN, "done\n");

	// Parse the kernel command line
	// This turned out to be unused, in favor of a config file in the initrd
#if 0
	if (mbd->flags & (1 << 2)) {
		// Duplicate it, so that we know that the address will be mapped when paging is enabled
		assert(mbd->cmdline != 0);
		kernel_cmdline = strdup((char *)mbd->cmdline);
		if (strstr(kernel_cmdline, "root=")) {
			rootdev = strdup(strstr(kernel_cmdline, "root=") + 5);
			char *p = rootdev;
			while (*p != 0 && *p != ' ') p++;
			*p = 0;
		}
	}

	printk("Parsed kernel command line; rootdev = %s\n", rootdev == NULL ? "NULL" : rootdev);
#endif

	/* Set up the keyboard callback */
	printk("Setting up the keyboard handler... ");
	init_keyboard();
	printc(BLACK, GREEN, "done\n");

	/* Set up the PIT and start counting ticks */
	printk("Initializing the PIT... ");
	timer_install();
	printc(BLACK, GREEN, "done\n");

	/* Initialize the initrd */
	/* (do this before paging, so that it doesn't end up in the kernel heap) */
	init_initrd(initrd_location);

	/* Set up paging and the kernel heap */
	printk("Initializing paging and setting up the kernel heap... ");

	//mbd->flags &= ~(1<<6); // Uncomment to test with no memory map
	if (mbd->flags & (1 << 6)) {
		// If the memory map is available
		init_paging(mbd->mmap_addr, mbd->mmap_length, mbd->mem_upper);
	}
	else
		init_paging(0, 0, mbd->mem_upper);

	printc(BLACK, GREEN, "done\n");

	printk("Detecting and initializing PCI devices... ");
	init_pci();
	printc(BLACK, GREEN, "done\n");

	/* Set up the syscall interface */
	printk("Initializing syscalls... ");
	init_syscalls();
	printc(BLACK, GREEN, "done\n");

	printk("Initializing multitasking and setting up the kernel task... ");
	init_tasking(init_esp0);
	printc(BLACK, GREEN, "done\n");

	printk("Starting network data handlers... ");
	nethandler_arp = nethandler_create("nethandler_arp", arp_handle_packet);
	nethandler_icmp = nethandler_create("nethandler_icmp", handle_icmp);
	if (nethandler_arp && nethandler_icmp)
		printc(BLACK, GREEN, "done\n");
	else
		printc(BLACK, RED, "failed!\n");

	create_task(cleanup_tasks, "cleanup_tasks", false, NULL, 0);

#if 1
	printk("Detecting ATA devices and initializing them... ");
	ata_init();
	printc(BLACK, GREEN, "done\n");

	/* Read the MBRs of the disks and set up the partitions array (devices[i].partitions[0...3]) */
	for (int i=0; i<3; i++)
		parse_mbr(&devices[i]);

	/* Detect FAT filesystems on all partitions */
	for (int disk = 0; disk < 4; disk++) {

		if (!devices[disk].exists || devices[disk].is_atapi)
			continue;

		for (int part = 0; part < 4; part++) {
			if (devices[disk].partition[part].exists && 
					(devices[disk].partition[part].type == PART_FAT32 ||
					 devices[disk].partition[part].type == PART_FAT32_LBA))
			{
				fat_detect(&devices[disk], part);
			}
		}
	}

	//assert(fat32_partitions !=  NULL);
	//assert(fat32_partitions->count == 1);
#endif

#if 0
	uint8 buf[512] = {0};
	ata_device_t *ata_dev = &devices[0];
	ata_read(ata_dev, 0, buf);
	printk("Buffer contents LBA0: \"%s\"\n", (char *)buf);

	((char *)buf)[0] = 'Y';
	ata_write(ata_dev, 0, buf);
#endif

#if 0
	/*
	 * DON'T ENABLE THIS without really taking care of having the correct disk image used!
	memset(buf, 0, 512);
	uint32 start_t = gettickcount();
	for (uint64 i = 10+0; i < 10+64000; i++) {
		assert(buf != NULL);
		ata_write(ata_dev, i, buf);
		//assert(buf != NULL);
		//if (*buf == 0)
			//continue;
		//printk("LBA%u: \"%s\"\n", i, (char *)buf);
		//assert(buf != NULL);
	}
	uint32 end_t = gettickcount();
	uint32 d = end_t - start_t;
	d *= 10;
	printk("Writing 64000 sectors took %u ms\n", d);
	*/
#endif

	printk("Mounting filesystems... ");
	if (fs_mount())
		printc(BLACK, GREEN, "done\n");
	else
		printc(BLACK, RED, "failed!\n");

#if 1
	printk("Initializing RTL8139 network adapter... ");
	if (init_rtl8139())
		printc(BLACK, GREEN, "done\n");
	else
		printc(BLACK, RED, "failed!\n");
#endif

	printk("All initialization complete!\n\n");

	virtual_consoles[0] = &kernel_console;
	assert(virtual_consoles[0]->active == true);


#if 1
	/* Set up the virtual consoles (Alt+F1 through F4 at the time of writing) */
	assert(NUM_VIRTUAL_CONSOLES >= 2); /* otherwise this loop will cause incorrect array access */
	for (int i=1 /* sic! */; i < NUM_VIRTUAL_CONSOLES; i++) {
		virtual_consoles[i] = console_create();
		/* node_t *new_node = */list_append(virtual_consoles[i]->tasks, create_task(&kshell, "kshell", virtual_consoles[i], NULL, 0));
		//((task_t *)new_node->data)->console = &virtual_consoles[i];
		assert(virtual_consoles[i]->active == false);
	}

	//console_switch(&virtual_consoles[0]);
#endif

	/* Hack-setup a kernel shell on the kernel console */
	assert(virtual_consoles[0] == &kernel_console);
	/*task_t *kernel_shell =*/ create_task(&kshell, "kshell", virtual_consoles[0], NULL, 0);

	while (true) {
		sleep(100000);
		//asm volatile("sti; hlt");
		//YIELD;
	}

	printk("\n\n");
	printk("kmain() done; running infinite loop\n");
	for(;;);
}
