#include <string.h>
#include <kernel/kernutil.h>
#include <kernel/heap.h>
#include <kernel/console.h>
#include <kernel/pci.h>
#include <kernel/vmm.h>
#include <kernel/timer.h>
#include <kernel/net/rtl8139.h>
#include <kernel/net/nethandler.h>
#include <kernel/net/arp.h>
#include <kernel/net/ipicmp.h>

static uint8 *rtl_mmio_base = NULL; // MMIO address to the card
static uint8 *recv_buf = NULL;      // RX Buffer used by the card
static uint32 recv_buf_phys = 0; // Physical address of the above
static uint8 *rtl8139_packetBuffer; // Where we copy the packet after reception

static uint8 current_descriptor = 0; // There are 4 TX descriptors (0-3)
static uint8 finish_descriptor = 0; // TODO
static sint8 free_descriptors = 4; // Should never go below 0, of course

extern nethandler_t *nethandler_arp;
extern nethandler_t *nethandler_icmp;

uint8 *my_mac = NULL; // set up in init

extern uint8 ip_address[4];

static uint8 rtl_byte_r(uint8 reg) {
	assert(rtl_mmio_base != 0);
	return *(rtl_mmio_base + reg);
}

static void rtl_byte_w(uint8 reg, uint8 val) {
	assert(rtl_mmio_base != 0);
	*(rtl_mmio_base + reg) = val;
}

static uint16 rtl_word_r(uint8 reg) {
	assert(rtl_mmio_base != 0);
	return *( (uint16 *)(rtl_mmio_base + reg));
}

static void rtl_word_w(uint8 reg, uint16 val) {
	assert(rtl_mmio_base != 0);
	*((uint16 *)(rtl_mmio_base + reg)) = val;
}
static uint32 rtl_dword_r(uint8 reg) {
	assert(rtl_mmio_base != 0);
	return *( (uint32 *)(rtl_mmio_base + reg));
}

static void rtl_dword_w(uint8 reg, uint32 val) {
	assert(rtl_mmio_base != 0);
	*((uint32 *)(rtl_mmio_base + reg)) = val;
}

static void rtl8139_reset(void) {
	assert(rtl_mmio_base != 0);
	*(rtl_mmio_base + RTL_CR) |= RTL_RESET;
	while (rtl_byte_r(RTL_CR) & RTL_RESET) { }
}

static txdesc_t TxDesc[4];

// A friend from ata.h
#define BSWAP16(x) ( (((x) & 0xff) << 8) | (((x) & 0xff00) >> 8) )

static void process_frame(uint16 packetLength) {
	//printk("process_frame of length %u\n", packetLength);

	ethheader_t *header = (ethheader_t *)(rtl8139_packetBuffer + 4);
	header->ethertype = BSWAP16(header->ethertype);

	if (header->ethertype == 0x8100) 
		panic("VLAN tag; fix this");
	else if (header->ethertype == ETHERTYPE_ARP) {
		//printk("\n*** ARP packet***\n");
		nethandler_add_packet(nethandler_arp, rtl8139_packetBuffer + 4 + sizeof(ethheader_t), packetLength - 8 /* header+CRC */ - sizeof(ethheader_t));
		set_next_task(nethandler_arp->task);
	}
	else if (header->ethertype == ETHERTYPE_IPV4) {
		//printk("IPv4 packet\n");
		ipv4header_t *v4 = (ipv4header_t *)(rtl8139_packetBuffer + 4 + sizeof(ethheader_t));
		//printk("version=%u IHL=%u total_length=%u\n", v4->version, v4->IHL, BSWAP16(v4->total_length));
		//printk("id=%u flags=%u fragment=%u ttl=%u proto=%u checksum=%04x src=%d.%d.%d.%d dst=%d.%d.%d.%d\n",
		//(uint32)v4->id, (uint32)v4->flags, (uint32)v4->fragment_offset, (uint32)v4->ttl, (uint32)v4->protocol, (uint32)BSWAP16(v4->header_checksum),
		//(uint32)v4->src_ip[0], (uint32)v4->src_ip[1], (uint32)v4->src_ip[2], (uint32)v4->src_ip[3],
		//(uint32)v4->dst_ip[0], (uint32)v4->dst_ip[1], (uint32)v4->dst_ip[2], (uint32)v4->dst_ip[3]);
		uint16 check = v4->header_checksum;
		v4->header_checksum = 0;

		// Number of additional bytes in the options field. If IHL == 5, there are none.
		//uint8 options_size = (v4->IHL - 5) * 4;

		if (v4->protocol == IPV4_PROTO_ICMP) {
			uint32 offset = 4 + sizeof(ethheader_t);// + sizeof(ipv4header_t) + options_size;
			// Pass the IPv4 packet(!), not just the ICMP bit
			nethandler_add_packet(nethandler_icmp, rtl8139_packetBuffer + offset, packetLength - offset);
			set_next_task(nethandler_icmp->task);
		}

		//printk("checksum=%04x (correct: %04x)\n", internet_checksum(v4, sizeof(ipv4header_t)), check);

		// TODO: this assumes packets will never be corrupt. Remove once the algorithm is tested!
		uint16 ret = internet_checksum((void *)v4, sizeof(ipv4header_t));
		assert(ret == check);
	}
	else if (header->ethertype == ETHERTYPE_IPV6) {
		//printk("IPv6 packet\n");
	}
	else {
		//printk("Unknown ethertype: 0x%04x\n", header->ethertype);
	}
}

static uint16 rxOffset = 0; /* Used to set CAPR value */

static uint32 rtl8139_rx_handler(uint32 esp) {
	// Exit if the RX buffer is empty
	if (rtl_byte_r(RTL_CR) & RTL_BUFE)
		return esp;

	do {
		uint8 *rxPointer = recv_buf + rxOffset;
		//uint16 flags = *( (uint16 *)rxPointer );
		uint16 packetLength = *( (uint16 *)(rxPointer + 2) );
		//printk("flags=[%04x] ", flags);

		// Clear the interrupt
		rtl_word_w(RTL_ISR, RTL_ROK); // TODO: should we clear all bits (0xe07f for the nonreserved bits) here?

		assert(packetLength <= 2048);
		//printk("received packet, len=%d\n", packetLength);

		// Copy this packet somewhere else
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
		rtl_word_w(RTL_CAPR, rxOffset - 0x10);

		// Do something with this frame, depending on what it is
		process_frame(packetLength);

	// ... and loop this while there are packets, in case another has arrived
	// since we began handling the one we were interrupted for
	} while (!(rtl_byte_r(RTL_CR) & RTL_BUFE));

	return esp;
}

static uint32 check_transmit_status(uint8 desc) {
	return rtl_dword_r(RTL_TSD_BASE + (desc *4)) & (RTL_TSD_OWN | RTL_TSD_TOK);
}

static uint32 rtl8139_tx_handler(uint32 esp) {
	// Clear the interrupt
	rtl_word_w(RTL_ISR, RTL_TOK); // TODO: should we clear all bits (0xe07f for the nonreserved bits) here?

	while (check_transmit_status(finish_descriptor) == RTL_TSD_BOTH && free_descriptors < 4) {
		// Release this buffer. Since this is barely documented this is mostly
		// from the (poorly written) programming guide.
		finish_descriptor = (finish_descriptor + 1) % 4;
		free_descriptors++;
		assert(free_descriptors >= 1 && free_descriptors <= 4);
	}

	//printk("sent packet\n");

	return esp;
}

uint32 rtl8139_interrupt_handler(uint32 esp) {
	// Check the reason for this interrupt
	uint16 isr = rtl_word_r(RTL_ISR);
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

	// Set up the Ethernet header
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

	// Set the physical address to the TX buffer
	rtl_dword_w(RTL_TSAD_BASE + current_descriptor*4, TxDesc[current_descriptor].buffer_phys);

	// Clear OWN bit, set length, and "disable" the early TX threshold by setting it to 1536 bytes
	// This will start the transmit process.
	rtl_dword_w(RTL_TSD_BASE + current_descriptor*4, (packetSize & 0xfff) | (48 << 16));

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

		my_mac = (uint8 *)(rtl_mmio_base); // MAC address is stored at register 0

		//recv_buf = kmalloc_ap(RTL8139_RXBUFFER_SIZE + 16, &recv_buf_phys);

		recv_buf = (uint8 *)0xff000000;
		recv_buf_phys = vmm_alloc_kernel((uint32)recv_buf, (uint32)(recv_buf + RTL8139_RXBUFFER_SIZE + PAGE_SIZE), PAGE_CONTINUOUS_PHYS, PAGE_RW);
		memset(recv_buf, 0, RTL8139_RXBUFFER_SIZE + PAGE_SIZE);

		rtl8139_packetBuffer = kmalloc(2048);
		memset(rtl8139_packetBuffer, 0, 2048);

		// Initialize 4 transmit descriptors, each with a 2k buffer
		for (int i=0; i < 4; i++) {
			TxDesc[i].buffer = kmalloc_ap(2048, &TxDesc[i].buffer_phys);
			TxDesc[i].packet_length = 0;
		}

		// Create the ARP cache
		arp_init();

		// Map the MMIO address to the virtual address space
		vmm_map_kernel((uint32)rtl_mmio_base, (uint32)rtl_mmio_base, PAGE_RW);

		/* "Turn on" the card (is this really necessary? QEMU works either way) */

		rtl_byte_w(RTL_CONFIG1, 0);

		/* Software reset to get started */
		rtl8139_reset();

		/* Register an IRQ handler */
		register_interrupt_handler(32 + dev->irq, rtl8139_interrupt_handler); /* IRQ:s are mapped to 32+ due to CPU exceptions being at 0-31 */

		/* Initialize the recieve buffer */
		rtl_dword_w(RTL_RBSTART, (uint32)recv_buf_phys);

		/* Set the Interrupt Mask Register to specify which interrupts we want */
		rtl_word_w(RTL_IMR, 0xffff); // TODO: remove some of these (cable length changed, for example)

		/* Configure the receive buffer register */
		/* 1 << 10 sets MXDMA to 100 (256 bytes, the maximum size DMA burst) */
		rtl_dword_w(RTL_RCR, RTL_AB | RTL_AM | RTL_APM | RTL_AAP | (1 << 10));

		/* Configure the Transmit configuration register */
		uint32 tcr = rtl_dword_r(RTL_TCR);
		tcr &= ~( (1 << 17) | (1 << 18) ); // Set loopback test mode bits to 00
		tcr &= ~( (1 << 16) ); // Append CRC (yes, 0 means append CRC; this seems to be a disable bit)
		tcr &= ~1; // Make sure the clear abort bit is not set
		tcr |= (6 << 8); // Set MXDMA bits to 110 (1024 bytes)
		rtl_dword_w(RTL_TCR, tcr);

		/* Start receiving (and allow transmitting)! */
		rtl_byte_w(RTL_CR, RTL_RE | RTL_TE);

	}
	else {
		//printk("No 8139 device found!\n");
		return false;
	}

	return true;
}
