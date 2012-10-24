#include <kernel/kheap.h>
#include <kernel/console.h>
#include <kernel/pci.h>
#include <kernel/rtl8139.h>

bool init_rtl8139(void) {
	pci_device_t *dev = find_pci_device(0x10ec /* Realtek */, 0x8139 /* RTL8139 */);
	if (dev) {
		printk("Found 8139 device!\n");
		return true;
	}
	else {
		printk("No 8139 device found!\n");
		return false;
	}
}
