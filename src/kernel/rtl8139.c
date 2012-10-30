#include <string.h>
#include <kernel/kernutil.h>
#include <kernel/kheap.h>
#include <kernel/console.h>
#include <kernel/pci.h>
#include <kernel/paging.h>
#include <kernel/rtl8139.h>
#include <kernel/timer.h>

static uint8 *rtl_mmio_base = NULL; // MMIO address to the card
static uint8 *recv_buf = NULL;      // RX Buffer used by the card
static uint32 recv_buf_phys = NULL; // Physical address of the above
static uint8 *rtl8139_packetBuffer; // Where we copy the packet after reception

static uint8 current_descriptor = 0; // There are 4 TX descriptors (0-3)
static uint8 finish_descriptor = 0; // TODO
static sint8 free_descriptors = 4; // Should never go below 0, of course

static uint8 ip_address[] = {192, 168, 10, 10}; // My IP address

// internet_checksum.s - not sure where to put this... TODO: move this
uint16 internet_checksum(void *ptr, uint32 length);

// TODO: rename these functions - rtl_r8/rtl_w8, rtlr16 ...? Surely something shorter than rtl_mmio_byte_r is possible.

// TODO temporarily here (and with a temporary name?)
void send_ipv4_packet(uint8 *dst_ip, uint8 protocol, void *payload, uint16 payload_size);

static uint8 rtl_mmio_byte_r(uint8 reg) {
	assert(rtl_mmio_base != 0);
	return *(rtl_mmio_base + reg);
}

static void rtl_mmio_byte_w(uint8 reg, uint8 val) {
	assert(rtl_mmio_base != 0);
	*(rtl_mmio_base + reg) = val;
}

static uint16 rtl_mmio_word_r(uint8 reg) {
	assert(rtl_mmio_base != 0);
	return *( (uint16 *)(rtl_mmio_base + reg));
}

static void rtl_mmio_word_w(uint8 reg, uint16 val) {
	assert(rtl_mmio_base != 0);
	*((uint16 *)(rtl_mmio_base + reg)) = val;
}
static uint32 rtl_mmio_dword_r(uint8 reg) {
	assert(rtl_mmio_base != 0);
	return *( (uint32 *)(rtl_mmio_base + reg));
}

static void rtl_mmio_dword_w(uint8 reg, uint32 val) {
	assert(rtl_mmio_base != 0);
	*((uint32 *)(rtl_mmio_base + reg)) = val;
}

static void rtl8139_reset(void) {
	assert(rtl_mmio_base != 0);
	*(rtl_mmio_base + RTL_CR) |= RTL_RESET;
	while (rtl_mmio_byte_r(RTL_CR) & RTL_RESET) { }
}

/* TODO: move to header file */
typedef struct {
	uint8 mac_dst[6];
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
	uint8 dst_mac[6]; // Target MAC; ignored for requests
	uint8 dst_ip[4];  // Target IP
} __attribute((packed)) arpheader_t;

// TODO: Should this stay or be moved?
typedef struct {
	uint8 *buffer;
	uint32 buffer_phys;
	uint16 packet_length;
} txdesc_t;

// TODO: move this
typedef struct {
	uint8 IHL : 4, version : 4;
	uint8 DSCP : 6, ECN : 2;
	uint16 total_length;
	uint16 id;
	uint16 flags : 3, fragment_offset : 13;
	uint8 ttl;
	uint8 protocol;
	uint16 header_checksum;
	uint8 src_ip[4]; // TODO: endianness?
	uint8 dst_ip[4]; // TODO: endianness?
} __attribute__((packed)) ipv4header_t;

static txdesc_t TxDesc[4];

// A friend from ata.h
#define BSWAP16(x) ( (((x) & 0xff) << 8) | (((x) & 0xff00) >> 8) )

void send_arp_reply(const uint8 *packet) {
	assert(packet != NULL);
	arpheader_t *header = (arpheader_t *)packet;
	uint8 buf[sizeof(arpheader_t)];
	uint8 tmp[6]; // used for swapping fields

	memcpy(buf, header, sizeof(arpheader_t));
	header = (arpheader_t *)header;

	/* Swap source and destination IPs */
	memcpy(tmp, header->dst_ip, 4);
	memcpy(header->dst_ip, header->src_ip, 4);
	memcpy(header->src_ip, tmp, 4);

	/* Set destination MAC to source MAC */
	memcpy(header->dst_mac, header->src_mac, 6);
	memcpy(header->src_mac, (rtl_mmio_base + 0), 6); // MAC is stored at offset 0x0 in MMIO

	header->operation = BSWAP16(ARP_REPLY);

	rtl8139_send_frame(header->dst_mac, ETHERTYPE_ARP, header, sizeof(arpheader_t));
}

void arp_handle_request(const uint8 *packet) {
	assert(packet != NULL);
	arpheader_t *header = (arpheader_t *)packet;
	if (BSWAP16(header->htype) != 1 || BSWAP16(header->ptype) != 0x0800) {
		printk("Ignoring non-Ethernet/IPv4 ARP request\n");
		return;
	}

	assert(header->hlen == 6);
	assert(header->plen == 4);
	assert(BSWAP16(header->operation) == ARP_REQUEST || BSWAP16(header->operation) == ARP_REPLY);
	printk("ARP info: Ethernet/IP ARP %s; source = %02x:%02x:%02x:%02x:%02x:%02x (%d.%d.%d.%d) dst = %02x:%02x:%02x:%02x:%02x:%02x (%d.%d.%d.%d)\n",
			(BSWAP16(header->operation) == ARP_REQUEST ? "request" : "reply"),
			header->src_mac[0], header->src_mac[1], header->src_mac[2], header->src_mac[3], header->src_mac[4], header->src_mac[5],
			header->src_ip[0], header->src_ip[1], header->src_ip[2], header->src_ip[3],
			header->dst_mac[0], header->dst_mac[1], header->dst_mac[2], header->dst_mac[3], header->dst_mac[4], header->dst_mac[5],
			header->dst_ip[0], header->dst_ip[1], header->dst_ip[2], header->dst_ip[3]);

	if (BSWAP16(header->operation) == ARP_REPLY)
		return;

	if (memcmp(header->dst_ip, ip_address, 4) == 0) {
		printk("This is for me! Creating and sending an ARP reply.\n");
		send_arp_reply(packet);
	}
	else
		printk("ARP request is for someone else, ignoring\n");
}

static void handle_icmp(uint8 *packet, uint16 length, uint8 *src_ip) {
	uint8 type = *packet;
	uint8 code = *(packet + 1);
	uint16 checksum = *((uint16 *)(packet + 2));
	checksum=checksum; // sigh
	code=code;

	switch (type) {
		case ICMP_ECHO_REQUEST: { // type 8
			uint16 identifier = *((uint16 *)(packet + 4));
			uint16 seq = *((uint16 *)(packet + 6));
			printk("ICMP echo request: id=%u seq=%u\n", BSWAP16(identifier), BSWAP16(seq));

			// Data length is the packet length, minus the 8 byte header
			printk("Data length: %u\n", length - 8);

			// TODO: don't alloc here...?
			uint8 *buf = kmalloc(length);
			memcpy(buf, packet, length);

			// Set the type field
			*buf = ICMP_ECHO_REPLY;

			// Calculate and set checksum
			*((uint16 *)(buf + 2)) = 0; // only zero to compute checksum
			*((uint16 *)(buf + 2)) = internet_checksum(buf, length);

			send_ipv4_packet(src_ip, IPV4_PROTO_ICMP, buf, length);

			break;
		}

		default:
			break;
	}
}

static void process_frame(uint16 packetLength) {
	printk("process_frame of length %u\n", packetLength);

	ethheader_t *header = (ethheader_t *)(rtl8139_packetBuffer + 4);
	header->ethertype = BSWAP16(header->ethertype);

	if (header->ethertype == 0x8100) 
		panic("VLAN tag; fix this");
	else if (header->ethertype == ETHERTYPE_ARP) {
		printk("ARP packet\n");
		arp_handle_request(rtl8139_packetBuffer + 4 + sizeof(ethheader_t)); // 4 bytes for the 8139 header
	}
	else if (header->ethertype == ETHERTYPE_IPV4) {
		printk("IPv4 packet\n");
		ipv4header_t *v4 = (ipv4header_t *)(rtl8139_packetBuffer + 4 + sizeof(ethheader_t));
		printk("version=%u IHL=%u total_length=%u\n", v4->version, v4->IHL, BSWAP16(v4->total_length));
		printk("id=%u flags=%u fragment=%u ttl=%u proto=%u checksum=%04x src=%d.%d.%d.%d dst=%d.%d.%d.%d\n",
				(uint32)v4->id, (uint32)v4->flags, (uint32)v4->fragment_offset, (uint32)v4->ttl, (uint32)v4->protocol, (uint32)BSWAP16(v4->header_checksum),
				(uint32)v4->src_ip[0], (uint32)v4->src_ip[1], (uint32)v4->src_ip[2], (uint32)v4->src_ip[3],
				(uint32)v4->dst_ip[0], (uint32)v4->dst_ip[1], (uint32)v4->dst_ip[2], (uint32)v4->dst_ip[3]);
		uint16 check = v4->header_checksum;
		v4->header_checksum = 0;

		// Number of additional bytes in the options field. If IHL == 5, there are none.
		uint8 options_size = (v4->IHL - 5) * 4;

		if (v4->protocol == IPV4_PROTO_ICMP) {
			handle_icmp(rtl8139_packetBuffer + 4 + sizeof(ethheader_t) + sizeof(ipv4header_t) + options_size, packetLength - 4 - sizeof(ethheader_t) - sizeof(ipv4header_t) - options_size, v4->src_ip);
		}

		printk("checksum=%04x (correct: %04x)\n", internet_checksum(v4, sizeof(ipv4header_t)), check);

		// TODO: this assumes packets will never be corrupt. Remove once the algorithm is tested!
		assert(internet_checksum((void *)v4, sizeof(ipv4header_t)) == check);
	}
	else if (header->ethertype == ETHERTYPE_IPV6) {
		printk("IPv6 packet\n");
	}
	else {
		printk("Unknown ethertype: 0x%04x\n", header->ethertype);
	}
}

static uint16 rxOffset = 0; /* Used to set CAPR value */

static uint32 rtl8139_rx_handler(uint32 esp) {
	// Exit if the RX buffer is empty
	if (rtl_mmio_byte_r(RTL_CR) & RTL_BUFE)
		return esp;

	do {
		uint8 *rxPointer = recv_buf + rxOffset;
		uint16 flags = *( (uint16 *)rxPointer );
		uint16 packetLength = *( (uint16 *)(rxPointer + 2) );
		printk("flags=[%04x] ", flags);

		static uint32 total_recv = 0;

		total_recv += packetLength;
		printk("total_recv = %u\n", total_recv);

		// Clear the interrupt
		rtl_mmio_word_w(RTL_ISR, RTL_ROK); // TODO: should we clear all bits (0xe07f for the nonreserved bits) here?

		// Copy this packet somewhere else
		assert(packetLength <= 2048);

		if (rxPointer + packetLength >= recv_buf + RTL8139_RXBUFFER_SIZE) {
			// This packet wraps around! Copy it in two parts.
			uint32 first_run = (recv_buf + RTL8139_RXBUFFER_SIZE) - rxPointer;
			memcpy(rtl8139_packetBuffer, rxPointer, first_run);
			memcpy(rtl8139_packetBuffer + first_run, recv_buf, packetLength - first_run);
		}
		else {
			// Easy.
			memcpy(rtl8139_packetBuffer, rxPointer, packetLength);
		}

		// Update the read pointer (CAPR). Goodness knows the logic behind this - I can't find much
		// usable information. The programming guide feels amateurish, but I assume their code works...
		rxOffset = (rxOffset + packetLength + 4 + 3) & ~0x3;
		rxOffset %= RTL8139_RXBUFFER_SIZE;
		rtl_mmio_word_w(RTL_CAPR, rxOffset - 0x10);

		// Do something with this frame, depending on what it is
		process_frame(packetLength);

	// ... and loop this while there are packets, in case another has arrived
	// since we began handling the one we were interrupted for
	} while (!(rtl_mmio_byte_r(RTL_CR) & RTL_BUFE));

	return esp;
}

static uint32 check_transmit_status(uint8 desc) {
	return rtl_mmio_dword_r(RTL_TSD_BASE + (desc *4)) & (RTL_TSD_OWN | RTL_TSD_TOK);
	/*
	switch (rtl_mmio_dword_r(RTL_TSD_BASE + (desc * 4)) & (RTL_TSD_OWN | RTL_TSD_TOK)) {
		case (RTL_TSD_OWN | RTL_TSD_TOK):
			return RTL_TSD_BOTH;
			break;
		case RTL_TSD_TOK:
			return RTL_TSD_TOK;
			break;
		case RTL_TSD_OWN:
			return RTL_TSD_OWN;
			break;
		case 0:
			return RTL_TSD_NONE;
			break;

	}
	*/
	return 0;
}

static uint32 rtl8139_tx_handler(uint32 esp) {
	// Clear the interrupt
	rtl_mmio_word_w(RTL_ISR, RTL_TOK); // TODO: should we clear all bits (0xe07f for the nonreserved bits) here?
	printk("in rtl8139_tx_handler()\n");

	while (check_transmit_status(finish_descriptor) == RTL_TSD_BOTH && free_descriptors < 4) {
		// Release this buffer. Since this is barely documented this is mostly
		// from the (poorly written) programming guide.
		finish_descriptor = (finish_descriptor + 1) % 4;
		free_descriptors++;
		printk("increased free_descriptors\n");
		assert(free_descriptors >= 1 && free_descriptors <= 4);
	}

	return esp;
}

uint32 rtl8139_interrupt_handler(uint32 esp) {
	// Check the reason for this interrupt
	uint16 isr = rtl_mmio_word_r(RTL_ISR);
	if (!(isr & RTL_ROK) && !(isr & RTL_TOK)) {
		// TODO: handle errors and such (and sign up for those interrupts)
		panic("RTL8139 interrupt without ROK/TOK");
	}
	if (isr & RTL_ROK)
		return rtl8139_rx_handler(esp);
	else if (isr & RTL_TOK)
		return rtl8139_tx_handler(esp);
	else {
		panic("Unhandled 8139 interrupt");
		return esp; // Silly, but gcc insists
	}
}

bool arp_lookup(uint8 *mac_buffer, uint8 *ip) {
	// Look up the MAC adress for "ip" and store it in mac_buffer
	mac_buffer=mac_buffer; ip=ip;
	memset(mac_buffer, 0xab, 6);
	return true;
}

void send_ipv4_packet(uint8 *dst_ip, uint8 protocol, void *payload, uint16 payload_size) {
	assert(dst_ip != NULL);
	assert(payload != NULL);
	assert(payload_size <= (1500 - sizeof(ipv4header_t)));

	// TODO: don't use kmalloc...?
	uint8 *buffer = kmalloc(sizeof(ipv4header_t) + payload_size);
	ipv4header_t *ip_hdr = (ipv4header_t *)buffer;
	ip_hdr->IHL = 5; // no options field
	ip_hdr->version = 4; // always 4 for IPv4
	ip_hdr->DSCP = 0; // not used
	ip_hdr->ECN = 0; // not used
	ip_hdr->total_length = BSWAP16(sizeof(ipv4header_t) + payload_size);
	ip_hdr->id = 0; // TODO: is this OK?
	ip_hdr->ttl = 64;
	ip_hdr->protocol = protocol;
	memcpy(ip_hdr->src_ip, ip_address, 4);
	memcpy(ip_hdr->dst_ip, dst_ip, 4);
	ip_hdr->header_checksum = 0; // Zero only to calculate the actual checksum
	ip_hdr->header_checksum = internet_checksum(ip_hdr, sizeof(ipv4header_t));

	memcpy(buffer + sizeof(ipv4header_t), payload, payload_size);

	uint8 dst_mac[6] = {0};
	assert(arp_lookup(dst_mac, dst_ip));

	printk("dst mac = %02x:%02x:%02x:%02x:%02x:%02x, dst ip = %d.%d.%d.%d\n",
			dst_mac[0], dst_mac[1], dst_mac[2], dst_mac[3], dst_mac[4], dst_mac[5],
			dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3]);

	dst_mac[0] = 0x10;
	dst_mac[1] = 0x10;
	dst_mac[2] = 0x10;
	dst_mac[3] = 0x20;
	dst_mac[4] = 0x20;
	dst_mac[5] = 0x20;

	//panic("Is that correct?");

	rtl8139_send_frame(dst_mac, ETHERTYPE_IPV4, buffer, sizeof(ipv4header_t) + payload_size);
}

void rtl8139_send_frame(uint8 *dst_mac, uint16 ethertype, void *payload, uint16 payload_size) {
	assert(dst_mac != NULL);
	assert(ethertype >= 0x0600); // Required for Ethernet II frames
	assert(payload != NULL);
	assert(payload_size > 0 && payload_size <= 1500);

	uint32 start = gettickcount();
	while (free_descriptors == 0) {
		// Wait! Or switch task? Sleep for 10 ms seems too long.
		if (gettickcount() > start + 100) {
			panic("no free descriptors for >1 second! Something's wrong somewhere.");
		}
	}

	ethheader_t *header = (ethheader_t *)TxDesc[current_descriptor].buffer;
	memcpy(header->mac_dst, dst_mac, 6);
	memcpy(header->mac_src, rtl_mmio_base + 0, 6); // register 0 holds our MAC address
	header->ethertype = BSWAP16(ethertype);

	// Copy the payload to the TX buffer
	memcpy(TxDesc[current_descriptor].buffer + sizeof(ethheader_t), payload, payload_size);

	// Calculate the packet size
	uint16 packetSize = payload_size + sizeof(ethheader_t);
	assert((packetSize & 0xfff) == packetSize);
	if (packetSize < 60) { // 64 minus the 4 CRC bytes
		memset(TxDesc[current_descriptor].buffer + packetSize, 0, 60 - packetSize); // Pad the rest with zeroes
		packetSize = 60;
	}

	TxDesc[current_descriptor].packet_length = packetSize;

	printk("to send: ");
	for (size_t i=0; i < payload_size + sizeof(ethheader_t); i++) {
		printk("%02x ", TxDesc[current_descriptor].buffer[i]);
	}
	printk("\n");

	// Set the physical address to the TX buffer
	rtl_mmio_dword_w(RTL_TSAD_BASE + current_descriptor*4, TxDesc[current_descriptor].buffer_phys);

	// Clear OWN bit, set length, and "disable" the early TX threshold by setting it to 1536 bytes
	// This will start the transmit process.
	rtl_mmio_dword_w(RTL_TSD_BASE + current_descriptor*4, (packetSize & 0xfff) | (48 << 16));

	// Move to the next TX descriptor (buffer etc.)
	current_descriptor = (current_descriptor + 1) % 4;
	free_descriptors--;
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

		recv_buf = kmalloc_ap(RTL8139_RXBUFFER_SIZE + 16, &recv_buf_phys);
		memset(recv_buf, 0, RTL8139_RXBUFFER_SIZE + 16);

		rtl8139_packetBuffer = kmalloc(2048);
		memset(rtl8139_packetBuffer, 0, 2048);

		//rtl8139_transmitBuffer = kmalloc_ap(2048, &rtl8139_transmitBuffer_phys);
		//memset(rtl8139_transmitBuffer, 0, 2048);

		// Initialize 4 transmit descriptors, each with a 2k buffer
		for (int i=0; i < 4; i++) {
			TxDesc[i].buffer = kmalloc_ap(2048, &TxDesc[i].buffer_phys);
			TxDesc[i].packet_length = 0;
		}

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
//		rtl_mmio_word_w(RTL_IMR, RTL_ROK | RTL_TOK); // TODO: add the rest of the useful ones, e.g. errors!
		rtl_mmio_word_w(RTL_IMR, 0xffff);

		/* Configure the receive buffer register */
		/* 1 << 10 sets MXDMA to 100 (256 bytes, the maximum size DMA burst) */
		rtl_mmio_dword_w(RTL_RCR, RTL_AB | RTL_AM | RTL_APM | RTL_AAP | (1 << 10));;// | RTL_WRAP);

		/* Configure the Transmit configuration register */
		uint32 tcr = rtl_mmio_dword_r(RTL_TCR);
		tcr &= ~( (1 << 17) | (1 << 18) ); // Set loopback test mode bits to 00
		tcr &= ~( (1 << 16) ); // Append CRC (yes, 0 means append CRC; this seems to be a disable bit)
		tcr &= ~1; // Make sure the clear abort bit is not set
		tcr |= (6 << 8); // Set MXDMA bits to 110 (1024 bytes)
		rtl_mmio_dword_w(RTL_TCR, tcr);

		/* Start receiving (and allow transmitting)! */
		rtl_mmio_byte_w(RTL_CR, RTL_RE | RTL_TE);

	}
	else {
		//printk("No 8139 device found!\n");
		return false;
	}

	return true;
}
