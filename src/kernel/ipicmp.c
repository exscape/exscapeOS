#include <string.h>
#include <kernel/kernutil.h>
#include <kernel/kheap.h>
#include <kernel/console.h>
#include <kernel/pci.h>
#include <kernel/paging.h>
#include <kernel/rtl8139.h>
#include <kernel/arp.h>
#include <kernel/ipicmp.h>
#include <kernel/timer.h>

uint8 ip_address[] = {192, 168, 10, 10}; // My IP address
uint8 gateway[]    = {192, 168, 10, 1};  // My default gw
uint8 netmask[]    = {255, 255, 255, 0}; // My netmask

// A friend from ata.h
#define BSWAP16(x) ( (((x) & 0xff) << 8) | (((x) & 0xff00) >> 8) )

// Are these two IPs on the same subnet?
// Uses the netmask global.
static bool same_subnet(uint8 *ip1, uint8 *ip2) {
	assert(ip1 != NULL);
	assert(ip2 != NULL);

	char buf1[4], buf2[4];
	memcpy(buf1, ip1, 4);
	memcpy(buf2, ip2, 4);

	for (int i=0; i<4; i++) {
		buf1[i] &= netmask[i];
		buf2[i] &= netmask[i];
	}

	return memcmp(buf1, buf2, 4) == 0;
}

//void handle_icmp(uint8 *packet, uint16 length, uint8 *src_ip) {
void handle_icmp(void *data, uint32 length) {
	uint8 *packet = (uint8 *)data + sizeof(ipv4header_t) + (((ipv4header_t *)data)->IHL - 5) * 4;
	uint8 type = *packet;
	uint8 code = *(packet + 1);
	uint16 checksum = *((uint16 *)(packet + 2));
	checksum=checksum; // sigh
	code=code;

	// We don't want to transmit 20 (usually) bytes too much!
	length -= sizeof(ipv4header_t);

	switch (type) {
		case ICMP_ECHO_REQUEST: { // type 8
			uint16 identifier = *((uint16 *)(packet + 4));
			uint16 seq = *((uint16 *)(packet + 6));
			printk("ICMP echo request: id=%u seq=%u\n", BSWAP16(identifier), BSWAP16(seq));

			// Data length is the packet length, minus the 8 byte header
			printk("Data length: %u\n", length - 8);

			// TODO: don't alloc here...?
			uint8 buf[1500];
			//uint8 *buf = kmalloc(length);
			memcpy(buf, packet, length);

			// Set the type field
			*buf = ICMP_ECHO_REPLY;

			// Calculate and set checksum
			*((uint16 *)(buf + 2)) = 0; // only zero to compute checksum
			*((uint16 *)(buf + 2)) = internet_checksum(buf, length);

			uint8 *src_ip = (uint8 *) (((ipv4header_t *)data)->src_ip);
			send_ipv4_packet(src_ip, IPV4_PROTO_ICMP, buf, length);

			//kfree(buf);

			break;
		}

		default:
			break;
	}
}

void send_ipv4_packet(uint8 *dst_ip, uint8 protocol, void *payload, uint16 payload_size) {
	assert(dst_ip != NULL);
	assert(payload != NULL);
	assert(payload_size <= (1500 - sizeof(ipv4header_t)));

	uint8 buffer[1500];
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

	// Is the target IP on the same subnet as we are?
	if (same_subnet(dst_ip, ip_address)) {
		// Yep - go ahead as usual
		assert(arp_lookup(dst_ip, dst_mac));
	}
	else {
		// No - send this packet to the gateway's MAC address instead
		assert(arp_lookup(gateway, dst_mac));
	}

	// TODO: arp_lookup() needs writing!
	//dst_mac[0] = 0x10;
	//dst_mac[1] = 0x10;
	//dst_mac[2] = 0x10;
	//dst_mac[3] = 0x20;
	//dst_mac[4] = 0x20;
	//dst_mac[5] = 0x20;

	printk("dst mac = %02x:%02x:%02x:%02x:%02x:%02x, dst ip = %d.%d.%d.%d\n",
			dst_mac[0], dst_mac[1], dst_mac[2], dst_mac[3], dst_mac[4], dst_mac[5],
			dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3]);
	//panic("Is that correct?");

	rtl8139_send_frame(dst_mac, ETHERTYPE_IPV4, buffer, sizeof(ipv4header_t) + payload_size);
}
