#ifndef _IPICMP_H
#define _IPICMP_H

#define IPV4_PROTO_ICMP	1
#define IPV4_PROTO_TCP	6
#define IPV4_PROTO_UDP	17

#define ICMP_ECHO_REQUEST	8
#define ICMP_ECHO_REPLY		0

typedef struct {
    uint8 IHL : 4, version : 4;
    uint8 DSCP : 6, ECN : 2;
    uint16 total_length;
    uint16 id;
    uint16 flags : 3, fragment_offset : 13;
    uint8 ttl;
    uint8 protocol;
    uint16 header_checksum;
    uint8 src_ip[4]; 
    uint8 dst_ip[4]; 
} __attribute__((packed)) ipv4header_t;

// internet_checksum.s - used for things besides IP and ICMP, though
uint16 internet_checksum(void *ptr, uint32 length);

void send_ipv4_packet(uint8 *dst_ip, uint8 protocol, void *payload, uint16 payload_size);
void handle_icmp(void *data, uint32 length);

#endif
