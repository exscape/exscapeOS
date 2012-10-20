#ifndef _PCI_H
#define _PCI_H

/* The IO registers used to access PCI registers */
#define PCI_CONFIG_ADDRESS 0xcf8
#define PCI_CONFIG_DATA 0xcfc

#define PCI_ENABLE (1UL << 31)

void init_pci(void);

#endif
