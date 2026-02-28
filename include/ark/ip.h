#pragma once

#include "ark/types.h"

// IP address representation
typedef struct {
    u8 a;
    u8 b;
    u8 c;
    u8 d;
} ip_addr_t;

// Network configuration
typedef struct {
    ip_addr_t local_ip;      // Our IP address
    ip_addr_t netmask;       // Network mask
    ip_addr_t gateway;       // Default gateway
    ip_addr_t dns;           // DNS server
    u8 mac[6];          // MAC address
    int configured;          // 1 if configured, 0 otherwise
} net_config_t;

// IPv4 header
typedef struct {
    u8 version_ihl;     // Version (4 bits) + IHL (4 bits)
    u8 dscp_ecn;        // DSCP (6 bits) + ECN (2 bits)
    u16 total_length;   // Total length of packet
    u16 identification; // ID for reassembly
    u16 flags_offset;   // Flags (3 bits) + Fragment offset (13 bits)
    u8 ttl;             // Time to live
    u8 protocol;        // Protocol (TCP=6, UDP=17, ICMP=1)
    u16 checksum;       // Header checksum
    u32 src_ip;         // Source IP address
    u32 dst_ip;         // Destination IP address
} ipv4_hdr_t;

// DHCP packet structure (simplified)
typedef struct {
    u8 op;              // Operation (1=request, 2=reply)
    u8 htype;           // Hardware type (1=Ethernet)
    u8 hlen;            // Hardware address length
    u8 hops;            // Hops
    u32 xid;            // Transaction ID
    u16 secs;           // Seconds elapsed
    u16 flags;          // Flags
    u32 ciaddr;         // Client IP address
    u32 yiaddr;         // Your IP address
    u32 siaddr;         // Server IP address
    u32 giaddr;         // Gateway IP address
    u8 chaddr[16];      // Client hardware address
    u8 sname[64];       // Server name
    u8 file[128];       // Boot file name
    u32 magic;          // Magic cookie (0x63825363)
    u8 options[308];    // DHCP options
} dhcp_packet_t;

// Global network configuration
extern net_config_t g_net_config;

// IP initialization functions
void ip_init(void);
void ip_set_static(ip_addr_t ip, ip_addr_t mask, ip_addr_t gateway);
void ip_set_mac(u8 *mac);

// IP utilities
u32 ip_to_uint32(ip_addr_t ip);
ip_addr_t u32o_ip(u32 val);
void ip_print(ip_addr_t ip);

// DHCP functions
void dhcp_request(void);
int dhcp_poll(void);

// Packet handling
void ip_handle_packet(void *packet, u16 len);

// Poll for incoming packets from the active network driver
void ip_poll(void);
