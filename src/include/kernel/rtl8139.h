#ifndef _RTL8139_H
#define _RTL8139_H

#define RTL8139_RXBUFFER_SIZE	(8192 + 16)

// RTL8139C registers
#define RTL_RBSTART 0x30 /* Receive Buffer Start Address (DWORD aligned) */
#define RTL_CR		0x37 /* command register */
#define RTL_CAPR	0x38 /* Current Address of Packet Read */
#define RTL_CBR		0x3a /* Current Buffer Address (Rx buffer) */
#define RTL_IMR		0x3c /* Interrupt Mask Register */
#define RTL_ISR		0x3e /* Interrupt Status Register */
#define RTL_TCR		0x40 /* Transmit Configuration Register */
#define RTL_RCR		0x44 /* Receive Configuration Register */
#define RTL_MPC		0x4c /* Missed Packet Counter */
#define RTL_CONFIG0 0x51 /* Configuration Register 0 */
#define RTL_CONFIG1 0x52 /* Configuration Register 1 */
#define RTL_CONFIG3 0x59 /* Configuration Register 3 */
#define RTL_CONFIG4 0x5a /* Configuration Register 4 */
#define RTL_BMCR	0x62 /* Basic Mode Control Register */
#define RTL_BMSR	0x64 /* Basic Mode Status Register */
#define RTL_CONFIG5 0xd8 /* Configuration Register 5 */

// Command register bits
#define RTL_RESET (1 << 4) /* Software Reset */
#define RTL_RE (1 << 3) /* Receiver Enable */
#define RTL_TE (1 << 2) /* Transmitter Enable */
#define RTL_BUFE (1 << 0) /* Receive Buffer Empty */

// IMR register bits
#define RTL_ROK (1 << 0)
#define RTL_RER (1 << 1)
#define RTL_TOK (1 << 2)
#define RTL_TER (1 << 3)

// RCR register bits
#define RTL_AAP  (1 << 0) /* Accept Physical Address Packets */
#define RTL_APM  (1 << 1) /* Accept Physical Match Packets */
#define RTL_AM   (1 << 2) /* Accept Multicast Packets */
#define RTL_AB	 (1 << 3) /* Accept Broadcast Packets */
#define RTL_WRAP (1 << 7)

// Ethertypes we might encounter
#define ETHERTYPE_IPv4	0x0800
#define ETHERTYPE_ARP	0x0806
#define ETHERTYPE_IPv6	0x86dd

// ARP stuff. TODO: move to separate source files
#define ARP_REQUEST 1
#define ARP_REPLY   2

bool init_rtl8139(void);

#endif
