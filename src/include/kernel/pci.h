#ifndef _PCI_H
#define _PCI_H

typedef struct {
	uint32 address;
	uint8 type; /* BAR_MEM or BAR_IO */
} bar_t;

typedef struct {
	uint16 vendor_id;
	uint16 device_id;
	bar_t bar[6];
	uint8 irq;
	uint8 classcode;
	uint8 subclasscode;
} pci_device_t;

// Looks through the PCI database to find the details (BARs, IRQ etc.)
// for a PCI device
pci_device_t *find_pci_device(uint32 vendor_id, uint32 device_id);

/* The IO registers used to access PCI registers */
#define PCI_CONFIG_ADDRESS 0xcf8
#define PCI_CONFIG_DATA 0xcfc

/* PCI configuration space offsets
 * comments after the value state what to AND the 32-bit register with 
 * to get the final value
 */
#define PCI_CONF_DEVICE			0x00 /* 0xffff0000 */
#define PCI_CONF_VENDOR			0x00 /* 0x0000ffff */
#define PCI_CONF_STATUS			0x04 /* 0xffff0000 */
#define PCI_CONF_COMMAND		0x04 /* 0x0000ffff */
#define PCI_CONF_CLASS			0x08 /* 0xffffff00 */
#define PCI_CONF_REVISION		0x08 /* 0x000000ff */
#define PCI_CONF_BIST			0x0c /* 0xff000000 */
#define PCI_CONF_HEADER_TYPE	0x0c /* 0x00ff0000 */
#define PCI_CONF_LATENCY		0x0c /* 0x0000ff00 */
#define PCI_CONF_CACHELINE_SIZE 0x0c /* 0x000000ff */
#define PCI_CONF_BAR0			0x10
#define PCI_CONF_BAR1			0x14
#define PCI_CONF_BAR2			0x18
#define PCI_CONF_BAR3			0x1c
#define PCI_CONF_BAR4			0x20
#define PCI_CONF_BAR5			0x24
#define PCI_CONF_IRQ			0x3C /* 0x000000ff */

/* bit 0 has these values for the two BAR types */
#define BAR_MEM 0
#define BAR_IO 1

#define PCI_ENABLE (1UL << 31)

void init_pci(void);

#endif
