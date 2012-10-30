#include <string.h>
#include <kernel/arp.h>
#include <kernel/kernutil.h>
#include <kernel/kheap.h>
#include <kernel/console.h>
#include <kernel/pci.h>
#include <kernel/paging.h>
#include <kernel/rtl8139.h>
#include <kernel/timer.h>

extern uint8 ip_address[4];

#define BSWAP16(x) ( (((x) & 0xff) << 8) | (((x) & 0xff00) >> 8) )

// rtl8139.c
extern uint8 *my_mac;

void send_arp_reply(const uint8 *packet) {
	assert(packet != NULL);
	arpheader_t *header = (arpheader_t *)packet;
	uint8 buf[sizeof(arpheader_t)];
	uint8 tmp[6]; // used for swapping fields

	// Copy the request packet
	memcpy(buf, header, sizeof(arpheader_t));
	header = (arpheader_t *)header;

	/* Swap source and destination IPs */
	memcpy(tmp, header->dst_ip, 4);
	memcpy(header->dst_ip, header->src_ip, 4);
	memcpy(header->src_ip, tmp, 4);

	/* Set destination MAC to source MAC */
	memcpy(header->dst_mac, header->src_mac, 6);
	memcpy(header->src_mac, my_mac, 6);

	/* Set the operation type to REPLY */
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

bool arp_lookup(uint8 *mac_buffer, uint8 *ip) {
	// Look up the MAC adress for "ip" and store it in mac_buffer
	mac_buffer=mac_buffer; ip=ip;
	memset(mac_buffer, 0xab, 6);
	return true;
}
