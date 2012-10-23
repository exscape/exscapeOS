#ifndef _PCI_H
#define _PCI_H

/* The IO registers used to access PCI registers */
#define PCI_CONFIG_ADDRESS 0xcf8
#define PCI_CONFIG_DATA 0xcfc


/* PCI configuration space offsets
 * NOTE: DEVICE and VENDOR share the same offset, because
 * they are part of the same DWORD. AND:ing and shifting must be done!
 */

#define PCI_CONF_DEVICE			0x00
#define PCI_CONF_VENDOR			0x00
#define PCI_CONF_STATUS			0x04
#define PCI_CONF_COMMAND		0x04
#define PCI_CONF_CLASS			0x08
#define PCI_CONF_REVISION		0x08
#define PCI_CONF_BIST			0x0c
#define PCI_CONF_HEADER_TYPE	0x0c
#define PCI_CONF_LATENCY		0x0c
#define PCI_CONF_CACHELINE_SIZE 0x0c
#define PCI_CONF_BAR0			0x10
#define PCI_CONF_BAR1			0x14
#define PCI_CONF_BAR2			0x18
#define PCI_CONF_BAR3			0x1c
#define PCI_CONF_BAR4			0x20
#define PCI_CONF_BAR5			0x24
/* anything later is not used yet */

#define PCI_ENABLE (1UL << 31)

void init_pci(void);

#endif
