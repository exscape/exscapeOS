#include <types.h>
#include <kernel/pci.h>
#include <kernel/kernutil.h>

/* Is this used? */
#include <kernel/kheap.h>

/* For debugging */
#include <kernel/console.h>

uint32 read_pci_config(uint8 bus, uint8 slot, uint8 func, uint8 reg) {
	outl(PCI_CONFIG_ADDRESS, PCI_ENABLE | (bus << 16) | (slot << 11) | (func << 8) | (reg & ~0x3));
	return inl(PCI_CONFIG_DATA);
//	return (unsigned short)((inl (0xCFC) >> ((offset & 2) * 8)) & 0xffff);
}

void init_pci(void) {
	uint32 bus, slot, func;

	for (bus = 0; bus < 256; bus++) {
		for (slot = 0; slot < 32; slot++) {
			for (func = 0; func < 8; func++) {
				uint32 vendor_id, device_id;
				uint32 bar0, bar1, bar2, bar3, bar4, bar5;

				vendor_id = (read_pci_config(bus, slot, func, PCI_CONF_VENDOR) & 0xffff);
				if (vendor_id == 0xffff)
					continue;

				device_id = (read_pci_config(bus, slot, func, PCI_CONF_DEVICE) & 0xffff0000) >> 16;

				printk("device found at bus %u, slot %u, func %u: vendor 0x%04x, device 0x%04x\n", bus, slot, func, vendor_id, device_id);

				if (/* header type == 00 */ true) {
					bar0 = read_pci_config(bus, slot, func, 0x10);
					bar1 = read_pci_config(bus, slot, func, 0x14);
					bar2 = read_pci_config(bus, slot, func, 0x18);
					bar3 = read_pci_config(bus, slot, func, 0x1c);
					bar4 = read_pci_config(bus, slot, func, 0x20);
					bar5 = read_pci_config(bus, slot, func, 0x24);

					printk(" bar0 0x%08x bar1 0x%08x bar2 0x%08x bar3 0x%08x bar4 0x%08x bar5 0x%08x\n", bar0, bar1, bar2, bar3, bar4, bar5);
				}
			}
		}
	}

	panic("halt");
}
