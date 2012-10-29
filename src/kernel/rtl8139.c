#include <string.h>
#include <kernel/kernutil.h>
#include <kernel/kheap.h>
#include <kernel/console.h>
#include <kernel/pci.h>
#include <kernel/paging.h>
#include <kernel/rtl8139.h>

static uint8 *rtl_mmio_base = NULL; // MMIO address to the card
static uint8 *recv_buf = NULL;      // RX Buffer used by the card
static uint32 recv_buf_phys = NULL; // Physical address the above
static uint8 *rtl8139_packetBuffer; // Where we copy the packet after reception

static uint8 ip_address[] = {192, 168, 10, 10}; // My IP address

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

static void rtl_mmio_word_w(uint8 reg, uint16 val) {
	assert(rtl_mmio_base != 0);
	*((uint16 *)(rtl_mmio_base + reg)) = val;
}
static uint32 rtl_mmio_dword_r(uint8 reg) {
	return *( (uint32 *)(rtl_mmio_base + reg));
}

static void rtl_mmio_dword_w(uint8 reg, uint32 val) {
	assert(rtl_mmio_base != 0);
	*((uint32 *)(rtl_mmio_base + reg)) = val;
}

static void rtl8139_reset(void) {
	*(rtl_mmio_base + RTL_CR) |= RTL_RESET;
	while (rtl_mmio_byte_r(RTL_CR) & RTL_RESET) { }
}

/* TODO: move to header file */
typedef struct {
	uint8 mac_dest[6];
	uint8 mac_src[6];
	/* uint32 vlan_tag; */
	uint16 ethertype;
	/* payload 42-1500 octets */
	/* uint32 crc; */
} __attribute((packed)) ethheader_t;

/* TODO: move */
typedef struct {
	uint16 htype; // 1 for Ethernet
	uint16 ptype; // 0x0800 for IPv4
	uint8 hlen;   // 6 for Ethernet addresses
	uint8 plen;   // 4 for IPv4 addresses
	uint16 operation; // ARP_REQUEST or ARP_REPLY
	uint8 src_mac[6]; // Sender MAC
	uint8 src_ip[4];  // Sender IP
	uint8 tgt_mac[6]; // Target MAC; ignored for requests
	uint8 tgt_ip[4];  // Target IP
} __attribute((packed)) arpheader_t;

// A friend from ata.h
#define BSWAP16(x) ( (((x) & 0xff) << 8) | (((x) & 0xff00) >> 8) )

void arp_handle_request(uint8 *packet) {
	arpheader_t *header = (arpheader_t *)packet;
	assert(BSWAP16(header->htype) == 1);
	assert(BSWAP16(header->ptype) == 0x0800);
	assert(header->hlen == 6);
	assert(header->plen == 4);
	assert(BSWAP16(header->operation) == ARP_REQUEST || BSWAP16(header->operation) == ARP_REPLY);
	printk("ARP info: Ethernet/IP ARP %s; source = %02x:%02x:%02x:%02x:%02x:%02x (%d.%d.%d.%d) dest = %02x:%02x:%02x:%02x:%02x:%02x (%d.%d.%d.%d)\n",
			(BSWAP16(header->operation) == ARP_REQUEST ? "request" : "reply"),
			header->src_mac[0], header->src_mac[1], header->src_mac[2], header->src_mac[3], header->src_mac[4], header->src_mac[5],
			header->src_ip[0], header->src_ip[1], header->src_ip[2], header->src_ip[3],
			header->tgt_mac[0], header->tgt_mac[1], header->tgt_mac[2], header->tgt_mac[3], header->tgt_mac[4], header->tgt_mac[5],
			header->tgt_ip[0], header->tgt_ip[1], header->tgt_ip[2], header->tgt_ip[3]);

	if (memcmp(header->tgt_ip, ip_address, 4) == 0) {
		printk("This is for me!\n");
	}
	else
		printk("ARP is for someone else\n");
}

void process_frame(uint16 packetLength) {
	//printk("process_frame of length %u\n", packetLength);

	printk("len=%u: ", packetLength);
	for (uint16 i=0; i < packetLength; i++) {
		printk("%02x ", rtl8139_packetBuffer[i]);
	}

	ethheader_t *header = (ethheader_t *)(rtl8139_packetBuffer + 4);
	header->ethertype = BSWAP16(header->ethertype);

	if (header->ethertype == 0x8100) 
		panic("VLAN tag; fix this");
	else if (header->ethertype == ETHERTYPE_ARP) {
		arp_handle_request(rtl8139_packetBuffer + 4 + 14); // 4 bytes for the 8139 header, 14 for the Ethernet frame header
		printk("\nARP packet\n");
	}
	else if (header->ethertype == ETHERTYPE_IPv4) {
		printk("\nIPv4 packet\n");
	}
	else if (header->ethertype == ETHERTYPE_IPv6) {
		printk("\nIPv6 packet\n");
	}
	else {
		printk("\nUnknown ethertype: 0x%04x\n", header->ethertype);
	}

	/* Kom ihåg +4 */

	printk("\n\n");
}

static uint16 rxOffset = 0; /* Used to set CAPR value */

uint32 rtl8139_interrupt_handler(uint32 esp) {
	//printk("*** RTL8139 INTERRUPT ***");

	// Exit if the RX buffer is empty
	if (rtl_mmio_byte_r(RTL_CR) & RTL_BUFE)
		return esp;

	do {
		uint8 *rxPointer = recv_buf + rxOffset;
		uint16 flags = *( (uint16 *)rxPointer );
		uint16 packetLength = *( (uint16 *)(rxPointer + 2) );
		printk("flags=[%04x] ", flags);

		// Check the reason for this interrupt
		uint16 isr = rtl_mmio_word_r(RTL_ISR);
		if (!(isr & RTL_ROK)) {
			// So far, we only recieve
			panic("RTL8139 interrupt without ROK");
		}

		assert(flags & 1); // ROK

		//printk("ISR = %04x (ROK = %u, RER = %u), ", isr, isr & RTL_ROK, isr & RTL_RER);
		// Clear the interrupt by writing all non-reserved bits in ISR
		rtl_mmio_word_w(RTL_ISR, 0xe07f); 

		//uint16 capr = rtl_mmio_word_r(RTL_CAPR);
		//printk(" CAPR = %04x ", capr);

		//uint16 cbr = rtl_mmio_word_r(RTL_CBR);
		//printk(" RTL_CBR = %04x ", cbr);

		// Copy this packet somewhere else
		assert(packetLength <= 2048);
		memcpy(rtl8139_packetBuffer, rxPointer, packetLength);

		// Update the read pointer (CAPR). Goodness knows the logic behind this - I can't find much
		// usable information. The programming guide feels amateurish, but I assume their code works...
		rxOffset = (rxOffset + packetLength + 4 + 3) & ~0x3;
		rxOffset %= RTL8139_RXBUFFER_SIZE;
		rtl_mmio_word_w(RTL_CAPR, rxOffset - 0x10);

		process_frame(packetLength);

		return esp;
	} while (!(rtl_mmio_byte_r(RTL_CR) & RTL_BUFE));
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

		recv_buf = kmalloc_ap(RTL8139_RXBUFFER_SIZE, &recv_buf_phys);
		memset(recv_buf, 0, RTL8139_RXBUFFER_SIZE);

		rtl8139_packetBuffer = kmalloc(2048);
		memset(rtl8139_packetBuffer, 0, 2048);

		map_phys_to_virt((uint32)rtl_mmio_base, (uint32)rtl_mmio_base, true /* kernel mode */, true /* writable */);

		/* "Turn on" the card (is this really necessary?) */
		rtl_mmio_byte_w(RTL_CONFIG1, 0);

		/* Software reset to get started */
		rtl8139_reset();

		/* Register an IRQ handler */
		register_interrupt_handler(32 + dev->irq, rtl8139_interrupt_handler); /* IRQ:s are mapped to 32+ due to CPU exceptions being at 0-31 */

		/* Initialize the recieve buffer */
		rtl_mmio_dword_w(RTL_RBSTART, (uint32)recv_buf_phys);

		/* Set the Interrupt Mask Register to specify which interrupts we want */
		//rtl_mmio_word_w(RTL_IMR, RTL_ROK | RTL_TOK); // TODO: add the rest of the useful ones, e.g. errors!
		rtl_mmio_word_w(RTL_IMR, RTL_ROK);

		/* Configure the receive buffer register */
		rtl_mmio_dword_w(RTL_RCR, RTL_AB | RTL_AM | RTL_APM | RTL_AAP);// | RTL_WRAP);

		/* Start receiving (and allow transmitting)! */
		rtl_mmio_byte_w(RTL_CR, RTL_RE | RTL_TE);

	}
	else {
		//printk("No 8139 device found!\n");
		return false;
	}

	return true;
}
