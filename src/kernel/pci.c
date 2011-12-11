#include <types.h>
#include <kernel/pci.h>
#include <kernel/kernutil.h>

/* Is this used? */
#include <kernel/kheap.h>

/* For debugging */
#include <kernel/console.h>

uint32 read_pci_config(uint8 bus, uint8 slot, uint8 func, uint8 offset) {
	outl(PCI_CONFIG_ADDRESS, 0x80000000 | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xfc));
	return inl(PCI_CONFIG_DATA);
}

void init_pci(void) {
	uint32 bus, slot, func;

	for (bus = 0; bus < 256; bus++) {
		for (slot = 0; slot < 32; slot++) {
			for (func = 0; func < 8; func++) {
				uint32 class;
				//uint8 type;
				class = read_pci_config(bus, slot, func, 0x08);
				uint32 bar0, bar1, bar2, bar3, bar4, bar5;
				if (class == 0xffffffff)
					continue;

				printk("device found at bus %u, slot %u, func %u: class 0x%08x\n", bus, slot, func, class);

				if (/* header type == 00 */ true) {
					bar0 = read_pci_config(bus, slot, func, 0x10);
					bar1 = read_pci_config(bus, slot, func, 0x14);
					bar2 = read_pci_config(bus, slot, func, 0x18);
					bar3 = read_pci_config(bus, slot, func, 0x1c);
					bar4 = read_pci_config(bus, slot, func, 0x20);
					bar5 = read_pci_config(bus, slot, func, 0x24);

					printk(" bar0 %08x bar1 %08x bar2 %08x bar3 %08x bar4 %08x bar5 %08x\n", bar0, bar1, bar2, bar3, bar4, bar5);
				}
			}
		}
	}

	panic("halt");
}
