#include <kernel/kernutil.h>
#include <kernel/kheap.h>
#include <kernel/console.h>
#include <kernel/pci.h>
#include <kernel/paging.h>
#include <kernel/rtl8139.h>

static uint8 *rtl_mmio_base = NULL;
static uint8 *recv_buf = NULL;
static uint32 recv_buf_phys = NULL;

static uint8 rtl_mmio_byte_r(uint8 reg) {
	return *(rtl_mmio_base + reg);
}

static void rtl_mmio_byte_w(uint8 reg, uint8 val) {
	assert(rtl_mmio_base != 0);
	*(rtl_mmio_base + reg) = val;
}

static uint16 rtl_mmio_word_r(uint8 reg) {
	return *( (uint16 *)(rtl_mmio_base + reg));
}

static void rtl_mmio_word_w(uint8 reg, uint8 val) {
	assert(rtl_mmio_base != 0);
	*((uint16 *)(rtl_mmio_base + reg)) = val;
}
static uint32 rtl_mmio_dword_r(uint8 reg) {
	return *( (uint32 *)(rtl_mmio_base + reg));
}

static void rtl_mmio_dword_w(uint8 reg, uint8 val) {
	assert(rtl_mmio_base != 0);
	*((uint32 *)(rtl_mmio_base + reg)) = val;
}

static void rtl8139_reset(void) {
	*(rtl_mmio_base + RTL_CR) |= RTL_RESET;
	while (rtl_mmio_byte_r(RTL_CR) & RTL_RESET) { }
}

uint32 rtl8139_interrupt_handler(uint32 esp) {
	printk("\"*** RTL8139 INTERRUPT ***\n");
	return esp;
}

bool init_rtl8139(void) {
	pci_device_t *dev = find_pci_device(0x10ec /* Realtek */, 0x8139 /* RTL8139 */);
	if (dev) {
		// Find the MMIO address
		for (int i=0; i < 6 /* 6 BARs in PCI */; i++) {
			if (dev->bar[i].type == BAR_MEM && dev->bar[i].address != 0) {
				rtl_mmio_base = (uint8 *)dev->bar[i].address;
				break;
			}
		}
		if (rtl_mmio_base == NULL) {
			return false;
		}

		recv_buf = kmalloc_ap(8192 + 16, &recv_buf_phys);

		map_phys_to_virt((uint32)rtl_mmio_base, (uint32)rtl_mmio_base, true /* kernel mode */, true /* writable */);

		/* "Turn on" the card (is this really necessary?) */
		rtl_mmio_byte_w(RTL_CONFIG1, 0);

		/* Software reset to get started */
		rtl8139_reset();

		/* Register an IRQ handler */
		register_interrupt_handler(32 + dev->irq, rtl8139_interrupt_handler); /* IRQ:s are mapped to 32+ due to CPU exceptions being at 0-31 */

		/* Initialize the recieve buffer */
		rtl_mmio_dword_w(RTL_RBSTART, (uint32)recv_buf);

		/* Set the Interrupt Mask Register to specify which interrupts we want */
		rtl_mmio_word_w(RTL_IMR, RTL_ROK | RTL_TOK); // TODO: add the rest of the useful ones, e.g. errors!

		/* Configure the receive buffer register */
		rtl_mmio_dword_w(RTL_RCR, RTL_AB | RTL_AM | RTL_APM | RTL_AAP | RTL_WRAP);

		/* Start receiving (and allow transmitting)! */
		rtl_mmio_byte_w(RTL_CR, RTL_RE | RTL_TE);

	}
	else {
		//printk("No 8139 device found!\n");
		return false;
	}

	return true;
}
