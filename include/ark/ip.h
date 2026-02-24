#pragma once

#include <stdint.h>

// IP address representation
typedef struct {
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint8_t d;
} ip_addr_t;

// Network configuration
typedef struct {
    ip_addr_t local_ip;      // Our IP address
    ip_addr_t netmask;       // Network mask
    ip_addr_t gateway;       // Default gateway
    ip_addr_t dns;           // DNS server
    uint8_t mac[6];          // MAC address
    int configured;          // 1 if configured, 0 otherwise
} net_config_t;

// IPv4 header
typedef struct {
    uint8_t version_ihl;     // Version (4 bits) + IHL (4 bits)
    uint8_t dscp_ecn;        // DSCP (6 bits) + ECN (2 bits)
    uint16_t total_length;   // Total length of packet
    uint16_t identification; // ID for reassembly
    uint16_t flags_offset;   // Flags (3 bits) + Fragment offset (13 bits)
    uint8_t ttl;             // Time to live
    uint8_t protocol;        // Protocol (TCP=6, UDP=17, ICMP=1)
    uint16_t checksum;       // Header checksum
    uint32_t src_ip;         // Source IP address
    uint32_t dst_ip;         // Destination IP address
} ipv4_hdr_t;

// DHCP packet structure (simplified)
typedef struct {
    uint8_t op;              // Operation (1=request, 2=reply)
    uint8_t htype;           // Hardware type (1=Ethernet)
    uint8_t hlen;            // Hardware address length
    uint8_t hops;            // Hops
    uint32_t xid;            // Transaction ID
    uint16_t secs;           // Seconds elapsed
    uint16_t flags;          // Flags
    uint32_t ciaddr;         // Client IP address
    uint32_t yiaddr;         // Your IP address
    uint32_t siaddr;         // Server IP address
    uint32_t giaddr;         // Gateway IP address
    uint8_t chaddr[16];      // Client hardware address
    uint8_t sname[64];       // Server name
    uint8_t file[128];       // Boot file name
    uint32_t magic;          // Magic cookie (0x63825363)
    uint8_t options[308];    // DHCP options
} dhcp_packet_t;

// Global network configuration
extern net_config_t g_net_config;

// IP initialization functions
void ip_init(void);
void ip_set_static(ip_addr_t ip, ip_addr_t mask, ip_addr_t gateway);
void ip_set_mac(uint8_t *mac);

// IP utilities
uint32_t ip_to_uint32(ip_addr_t ip);
ip_addr_t uint32_to_ip(uint32_t val);
void ip_print(ip_addr_t ip);

// DHCP functions
void dhcp_request(void);
int dhcp_poll(void);

// Packet handling
void ip_handle_packet(void *packet, uint16_t len);

// Poll for incoming packets from e1000
void ip_poll(void);
