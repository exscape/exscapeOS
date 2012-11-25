#ifndef _ARP_H
#define _ARP_H

#define ARP_REQUEST 1
#define ARP_REPLY   2

#define ARP_CACHE_TIME (5*60*1000) // 5 minutes

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

// An entry in the ARP cache
typedef struct {
    uint8 ip[4];
    uint8 mac[6];
    uint32 timestamp;
} arpentry_t;

void arp_init(void); // Sets up the ARP cache
void send_arp_reply(const uint8 *packet);
void arp_handle_packet(void *data, uint32 length);
bool arp_lookup(uint8 *ip, uint8 *mac_buffer);

#endif
