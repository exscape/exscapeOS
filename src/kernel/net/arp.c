#include <string.h>
#include <kernel/kernutil.h>
#include <kernel/heap.h>
#include <kernel/console.h>
#include <kernel/pci.h>
#include <kernel/vmm.h>
#include <kernel/timer.h>
#include <kernel/net/arp.h>
#include <kernel/net/nethandler.h>
#include <kernel/net/rtl8139.h>

extern uint8 ip_address[4];

#define BSWAP16(x) ( (((x) & 0xff) << 8) | (((x) & 0xff00) >> 8) )

// rtl8139.c
extern uint8 *my_mac;

list_t *arp_cache = NULL;

void arp_init(void) {
	if (arp_cache != NULL)
		return;

	arp_cache = list_create();
}

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

void arp_cache_add(uint8 *ip, uint8 *mac) {
	// Is this IP in the ARP cache?
	INTERRUPT_LOCK;
	list_foreach(arp_cache, it) {
		arpentry_t *entry = (arpentry_t *)it->data;
		if (memcmp(entry->ip, ip, 4) == 0) {
			// Yes - update this entry
			memcpy(entry->mac, mac, 6);
			entry->timestamp = gettickcount();
			INTERRUPT_UNLOCK;
			return;
		}
	}

	// If we got here, the IP was NOT in the ARP cache. Add it.
	arpentry_t *entry = kmalloc(sizeof(arpentry_t));
	memcpy(entry->ip, ip, 4);
	memcpy(entry->mac, mac, 6);
	entry->timestamp = gettickcount();

	list_append(arp_cache, entry);
	INTERRUPT_UNLOCK;
}

void arp_handle_packet(void *data, uint32 length) {
	//printk("*** arp_handle_packet ***\n");
	arpheader_t *header = (arpheader_t *)data;
	length = length; // make GCC shut up
	assert(data != NULL);
	assert(length >= sizeof(arpheader_t));
	if (BSWAP16(header->htype) != 1 || BSWAP16(header->ptype) != 0x0800) {
		printk("Ignoring non-Ethernet/IPv4 ARP request\n");
		return;
	}

	assert(header->hlen == 6);
	assert(header->plen == 4);
	assert(BSWAP16(header->operation) == ARP_REQUEST || BSWAP16(header->operation) == ARP_REPLY);

	//printk("ARP info: Ethernet/IP ARP %s; source = %02x:%02x:%02x:%02x:%02x:%02x (%d.%d.%d.%d) dst = %02x:%02x:%02x:%02x:%02x:%02x (%d.%d.%d.%d)\n",
	//(BSWAP16(header->operation) == ARP_REQUEST ? "request" : "reply"),
	//header->src_mac[0], header->src_mac[1], header->src_mac[2], header->src_mac[3], header->src_mac[4], header->src_mac[5],
	//header->src_ip[0], header->src_ip[1], header->src_ip[2], header->src_ip[3],
	//header->dst_mac[0], header->dst_mac[1], header->dst_mac[2], header->dst_mac[3], header->dst_mac[4], header->dst_mac[5],
	//header->dst_ip[0], header->dst_ip[1], header->dst_ip[2], header->dst_ip[3]);

	if (BSWAP16(header->operation) == ARP_REPLY) {
		// TODO: ARP spoofing and all that - is there a more secure solution to this?
		arp_cache_add(header->src_ip, header->src_mac);
		return;
	}
	else {
		// This is an ARP request

		// Add the sender to the cache (or update it) before we reply
		arp_cache_add(header->src_ip, header->src_mac);

		if (memcmp(header->dst_ip, ip_address, 4) == 0) {
			//printk("This is for me! Creating and sending an ARP reply.\n");
			send_arp_reply((uint8 *)data);
		}
		//else
			//printk("ARP request is for someone else, ignoring\n");
	}
}

bool arp_cache_lookup(uint8 *ip, uint8 *mac_buffer) {
	// Look up an IP->MAC mapping in the cache. If the entry isn't
	// in the cache, return false.
	// To actually perform a lookup, use arp_lookup().
	assert(ip != NULL);
	assert(mac_buffer != NULL);
	assert(arp_cache != NULL);

	// Loop through the ARP cache list
	// Perhaps not the best-suited data structure, but it works,
	// and with few hosts it's certainly fast enough.
	INTERRUPT_LOCK;
	list_foreach(arp_cache, it) {
		arpentry_t *entry = (arpentry_t *)it->data;
		if (memcmp(entry->ip, ip, 4) == 0) {
			// Found it!
			if ((gettickcount() - entry->timestamp) * (1000/TIMER_HZ) >= ARP_CACHE_TIME) {
				// This entry is too old! Let's dump it. Sorry, requester, you're out of luck.
				list_remove_node(arp_cache, it);
				kfree(entry);
				INTERRUPT_UNLOCK;
				return false;
			}

			// Looks like it's fresh enough, let's return it.
			memcpy(mac_buffer, entry->mac, 6);
			INTERRUPT_UNLOCK;
			return true;
		}
	}

	INTERRUPT_UNLOCK;
	return false;
}

bool arp_lookup(uint8 *ip, uint8 *mac_buffer) {
	// Check the ARP cache, and try to lookup an entry.
	// If not present, send an ARP request on the network.
	// Look up the MAC adress for "ip" and store it in mac_buffer
	assert(ip != NULL);
	assert(mac_buffer != NULL);

	// Try the cache first
	if (arp_cache_lookup(ip, mac_buffer) == true)
		return true;

	// Fill out the ARP request
	uint8 buf[sizeof(arpheader_t)];
	arpheader_t *header = (arpheader_t *)buf;
	header->htype = BSWAP16(1); // Ethernet
	header->ptype = BSWAP16(0x0800); // IP
	header->hlen = 6; // MAC address length
	header->plen = 4; // IP address length
	header->operation = BSWAP16(ARP_REQUEST);
	memcpy(header->src_mac, my_mac, 6);
	memcpy(header->src_ip, ip_address, 4);
	memset(header->dst_mac, 0, 6);
	memcpy(header->dst_ip, ip, 4);

	// Send to ff:ff:ff:ff:ff:ff
	uint8 broadcast_mac[6];
	memset(broadcast_mac, 0xff, 6);
	memset(mac_buffer, 0xff, 6); // Temporary value, to check whether it's been filled or not, below
	rtl8139_send_frame(broadcast_mac, ETHERTYPE_ARP, header, sizeof(arpheader_t));

	// Wait for the ARP cache to contain the answer,
	// OR for a timeout to occur.
	uint32 start = gettickcount();
	while (arp_cache_lookup(ip, mac_buffer) == false && gettickcount() < start + 200) {
		// TODO: sleep?
	}

	if (memcmp(mac_buffer, broadcast_mac, 6) == 0) {
		// arp_cache_lookup didn't change the content of mac_buffer.
		return false;
	}
	else {
		// We're done - the result has been stored to mac_buffer.
		return true;
	}
}
