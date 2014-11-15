#include <sys/types.h>
#include <kernel/pci.h>
#include <kernel/kernutil.h>
#include <kernel/timer.h>
#include <string.h> /* memset */

/* Is this used? */
#include <kernel/heap.h>

/* For debugging */
#include <kernel/console.h>

list_t *pci_devices = NULL;

#define PCI_DEBUG 0

pci_device_t *find_pci_device(uint32 vendor_id, uint32 device_id) {
	// list_node_find_next_predicate can't accept additional parameters (d'oh!),
	// so we'll have to do this ourselves.
	list_foreach(pci_devices, it) {
		pci_device_t *cur = (pci_device_t *)it->data;
		if (cur->vendor_id == vendor_id && cur->device_id == device_id)
			return cur;

	}

	return NULL;
}

// Reads the PCI configuration space, one dword at a time.
// To read 8 or 16 bits, you currently need to AND the rest out yourself.
uint32 pci_read_config(uint8 bus, uint8 slot, uint8 func, uint8 reg) {
	outl(PCI_CONFIG_ADDRESS, PCI_ENABLE | (bus << 16) | (slot << 11) | (func << 8) | (reg & ~0x3));
	return inl(PCI_CONFIG_DATA);
}

void init_pci(void) {
	uint32 bus, slot, func;

	pci_devices = list_create();

	for (bus = 0; bus < 256; bus++) {
		for (slot = 0; slot < 32; slot++) {
			for (func = 0; func < 8; func++) {
				uint32 vendor_id, device_id;

				vendor_id = (pci_read_config(bus, slot, func, PCI_CONF_VENDOR) & 0xffff);
				if (vendor_id == 0xffff)
					continue;

				device_id = (pci_read_config(bus, slot, func, PCI_CONF_DEVICE) & 0xffff0000) >> 16;
				uint32 class_tmp = pci_read_config(bus, slot, func, PCI_CONF_CLASS);

				uint32 classcode = (class_tmp >> 24);
				uint32 subclasscode = (class_tmp >> 16) & 0xff;

				//printk("device found at bus %u, slot %u, func %u: vendor 0x%04x, device 0x%04x\n", bus, slot, func, vendor_id, device_id);
				uint8 type = (pci_read_config(bus, slot, func, PCI_CONF_HEADER_TYPE) & 0x00ff0000) >> 24;

				/*
				if (type == 0)
					printk("Type: regular PCI device\n");
				else if (type == 1)
					printk("Type: PCI-to-PCI bridge\n");
				else if (type == 2)
					printk("Type: CardBus bridge\n");
				*/

				uint8 interrupt = pci_read_config(bus, slot, func, PCI_CONF_IRQ) & 0xff; // lower 8 bits is the interrupt
				//printk("IRQ: %u\n", interrupt);

				// Store this device in the PCI device database
				pci_device_t *dev = kmalloc(sizeof(pci_device_t));
				memset(dev, 0, sizeof(pci_device_t));
				dev->vendor_id = vendor_id;
				dev->device_id = device_id;
				dev->irq = interrupt;
				dev->classcode = classcode;
				dev->subclasscode = subclasscode;

				list_append(pci_devices, dev);
				// BARs are filled in below

				uint32 bar[6] = {0};
				if (type == 0) {
					for (int i=0; i<6; i++) {
						bar[i] = pci_read_config(bus, slot, func, 0x10 + i*4);
						if ((bar[i] & 1) == BAR_IO) {
							// This is an I/O space address; the 2 LSBs aren't part of the address
							dev->bar[i].type = BAR_IO;
							dev->bar[i].address = bar[i] & ~0x3;
							//printk("BAR%d is an IO address\n", i);
						}
						else {
							// Memory space address; 4 LSBs aren't part of the address (bit 3 is prefetch, 2-1 type, and bit 0 always 0)
							dev->bar[i].type = BAR_MEM;
							dev->bar[i].address = bar[i] & ~0xf;
							//if (bar[i] != 0) /* used for empty BARs */
								//printk("BAR%d is a memory address\n", i);
						}
					}

					//printk("PCI revision: 0x%02x\n", pci_read_config(bus, slot, func, PCI_CONF_REVISION) & 0xff);

					//printk(" bar0 0x%08x bar1 0x%08x bar2 0x%08x bar3 0x%08x bar4 0x%08x bar5 0x%08x\n", dev->bar[0].address, dev->bar[1].address, dev->bar[2].address, dev->bar[3].address, dev->bar[4].address, dev->bar[5].address);
					//printk("\n");
					//printk("database size is now %u items\n", pci_devices->count);
				}
			}
		}
	}

#if PCI_DEBUG > 0
	for (node_t *n = pci_devices->head; n != NULL; n = n->next) {
		pci_device_t *cur = (pci_device_t *)n->data;
		printk("%04x:%04x", cur->vendor_id, cur->device_id);
		if (cur->irq != 0)
			printk(", IRQ %u", cur->irq);
		for (int i=0; i<6; i++) {
			if (cur->bar[i].address != 0) {
				printk(" BAR%u (%s): 0x%08x", i, cur->bar[i].type == BAR_IO ? "IO" : "MEM", cur->bar[i].address);
			}
		}
		printk("\n");
	}
#endif
}
