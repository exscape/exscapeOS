#ifndef _ARP_H
#define _ARP_H

#define ARP_REQUEST 1
#define ARP_REPLY   2

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

void send_arp_reply(const uint8 *packet);
void arp_handle_request(const uint8 *packet);
bool arp_lookup(uint8 *mac_buffer, uint8 *ip);

#endif
