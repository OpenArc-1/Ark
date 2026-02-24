// ip.c - Real IP networking stack for Ark OS with e1000 driver

#include "ark/ip.h"
#include "ark/e1000.h"
#include "ark/printk.h"
#include "ark/mem.h"

#define ETHERNET_TYPE_ARP  0x0806
#define ETHERNET_TYPE_IPV4 0x0800

#define ARP_OPCODE_REQUEST 1
#define ARP_OPCODE_REPLY   2

// Ethernet frame header
typedef struct {
    uint8_t dest_mac[6];
    uint8_t src_mac[6];
    uint16_t type;
} eth_hdr_t;

// ARP packet
typedef struct {
    uint16_t hw_type;       // Hardware type (1 = Ethernet)
    uint16_t proto_type;    // Protocol type (0x0800 = IPv4)
    uint8_t hw_addr_len;    // Hardware address length (6 for Ethernet)
    uint8_t proto_addr_len; // Protocol address length (4 for IPv4)
    uint16_t opcode;        // Operation (1 = request, 2 = reply)
    uint8_t src_mac[6];
    uint32_t src_ip;
    uint8_t dest_mac[6];
    uint32_t dest_ip;
} arp_packet_t;

// Global network configuration
net_config_t g_net_config = {
    .local_ip = {0, 0, 0, 0},
    .netmask = {255, 255, 255, 0},
    .gateway = {0, 0, 0, 0},
    .dns = {8, 8, 8, 8},
    .mac = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56},
    .configured = 0
};

// DHCP state machine
static int dhcp_state = 0;     // 0=idle, 1=discovering, 2=requesting, 3=bound
static uint32_t dhcp_xid = 0;
static uint32_t dhcp_server_ip __attribute__((unused)) = 0;
static uint32_t dhcp_lease_time __attribute__((unused)) = 0;

// ARP cache
#define ARP_CACHE_SIZE 16
static struct {
    uint32_t ip;
    uint8_t mac[6];
    int valid;
} arp_cache[ARP_CACHE_SIZE];

// Convert IP address to uint32_t
uint32_t ip_to_uint32(ip_addr_t ip) {
    return ((uint32_t)ip.a << 24) | ((uint32_t)ip.b << 16) | 
           ((uint32_t)ip.c << 8) | (uint32_t)ip.d;
}

// Convert uint32_t to IP address
ip_addr_t uint32_to_ip(uint32_t val) {
    return (ip_addr_t){
        .a = (val >> 24) & 0xFF,
        .b = (val >> 16) & 0xFF,
        .c = (val >> 8) & 0xFF,
        .d = val & 0xFF
    };
}

// Print IP address in dotted decimal format
void ip_print(ip_addr_t ip) {
    printk("%d.%d.%d.%d", ip.a, ip.b, ip.c, ip.d);
}

// Set static IP configuration
void ip_set_static(ip_addr_t ip, ip_addr_t mask, ip_addr_t gateway) {
    g_net_config.local_ip = ip;
    g_net_config.netmask = mask;
    g_net_config.gateway = gateway;
    g_net_config.configured = 1;
    
    printk(T,"[IP] Static configuration set:\n");
    printk("     IP: ");
    ip_print(ip);
    printk("\n");
    printk("     Netmask: ");
    ip_print(mask);
    printk("\n");
    printk("     Gateway: ");
    ip_print(gateway);
    printk("\n");
}

// Set MAC address
void ip_set_mac(uint8_t *mac) {
    memcpy(g_net_config.mac, mac, 6);
    printk(T,"[IP] MAC address set: %02x:%02x:%02x:%02x:%02x:%02x\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// Calculate IPv4 checksum (for future use)
static uint16_t ipv4_checksum(void *data, int len) __attribute__((unused));
static uint16_t ipv4_checksum(void *data, int len) {
    uint32_t sum = 0;
    uint16_t *ptr = (uint16_t *)data;
    
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    if (len)
        sum += *(uint8_t *)ptr;
    
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    
    return ~sum;
}

// Send ethernet frame via e1000
static void eth_send(uint8_t *dest_mac, uint16_t type, void *payload, uint16_t payload_len) {
    // Allocate buffer with space for Ethernet header
    uint8_t frame_buf[2048];
    eth_hdr_t *eth = (eth_hdr_t *)frame_buf;
    uint8_t *frame_payload = frame_buf + sizeof(eth_hdr_t);
    
    // Build Ethernet header
    memcpy(eth->dest_mac, dest_mac, 6);
    memcpy(eth->src_mac, g_net_config.mac, 6);
    eth->type = ((type & 0xFF) << 8) | ((type >> 8) & 0xFF);  // Network byte order
    
    // Copy payload
    memcpy(frame_payload, payload, payload_len);
    
    // Send complete frame
    e1000_send(frame_buf, sizeof(eth_hdr_t) + payload_len);
    
    printk(T,"[ETH] Sent frame to %02x:%02x:%02x:%02x:%02x:%02x (type: 0x%04x)\n",
           dest_mac[0], dest_mac[1], dest_mac[2], dest_mac[3], dest_mac[4], dest_mac[5], type);
}

// Lookup MAC address in ARP cache (for future use)
static int arp_lookup(uint32_t ip, uint8_t *mac) __attribute__((unused));
static int arp_lookup(uint32_t ip, uint8_t *mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(mac, arp_cache[i].mac, 6);
            return 1;
        }
    }
    return 0;
}

// Add entry to ARP cache
static void arp_cache_add(uint32_t ip, uint8_t *mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            arp_cache[i].ip = ip;
            memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].valid = 1;
            printk(T,"[ARP] Cache: ");
            ip_print(uint32_to_ip(ip));
            printk(" -> %02x:%02x:%02x:%02x:%02x:%02x\n",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            return;
        }
    }
}

// Handle ARP request/reply
static void arp_handle_packet(arp_packet_t *arp) {
    uint16_t opcode = ((arp->opcode >> 8) & 0xFF) | ((arp->opcode & 0xFF) << 8);
    
    printk(T,"[ARP] Packet: opcode=%d, from ", opcode);
    ip_print(uint32_to_ip(arp->src_ip));
    printk("\n");
    
    // Cache the sender's MAC/IP mapping
    arp_cache_add(arp->src_ip, arp->src_mac);
    
    // If this is a request for our IP, send a reply
    if (opcode == ARP_OPCODE_REQUEST && 
        arp->dest_ip == ip_to_uint32(g_net_config.local_ip)) {
        printk(T,"[ARP] Sending reply for ");
        ip_print(uint32_to_ip(arp->dest_ip));
        printk("\n");
        
        // Build ARP reply
        arp_packet_t reply;
        reply.hw_type = arp->hw_type;
        reply.proto_type = arp->proto_type;
        reply.hw_addr_len = 6;
        reply.proto_addr_len = 4;
        reply.opcode = ((ARP_OPCODE_REPLY & 0xFF) << 8) | ((ARP_OPCODE_REPLY >> 8) & 0xFF);
        memcpy(reply.src_mac, g_net_config.mac, 6);
        reply.src_ip = ip_to_uint32(g_net_config.local_ip);
        memcpy(reply.dest_mac, arp->src_mac, 6);
        reply.dest_ip = arp->src_ip;
        
        eth_send(arp->src_mac, ETHERNET_TYPE_ARP, &reply, sizeof(reply));
    }
}

// Handle incoming IPv4 packet
static void ipv4_handle_packet(ipv4_hdr_t *hdr, uint16_t payload_len) {
    uint8_t protocol = hdr->protocol;
    
    printk(T,"[IP] IPv4 packet: proto=%d, from ", protocol);
    ip_print(uint32_to_ip(hdr->src_ip));
    printk(" to ");
    ip_print(uint32_to_ip(hdr->dst_ip));
    printk(" (len=%d)\n", payload_len);
    
    // Check if packet is for us
    if (hdr->dst_ip != ip_to_uint32(g_net_config.local_ip)) {
        printk(T,"[IP] Not for us, ignoring\n");
        return;
    }
    
    switch (protocol) {
        case 1:  // ICMP
            printk(T,"[IP] ICMP packet received\n");
            break;
        case 6:  // TCP
            printk(T,"[IP] TCP packet received\n");
            break;
        case 17: // UDP
            printk(T,"[IP] UDP packet received\n");
            break;
        default:
            printk(T,"[IP] Unknown protocol: %d\n", protocol);
    }
}

// Process incoming packet from e1000
void ip_handle_packet(void *packet, uint16_t len) {
    if (len < sizeof(eth_hdr_t)) {
        printk(T,"[IP] Packet too short\n");
        return;
    }
    
    eth_hdr_t *eth = (eth_hdr_t *)packet;
    uint16_t type = ((eth->type >> 8) & 0xFF) | ((eth->type & 0xFF) << 8);
    void *payload = (uint8_t *)packet + sizeof(eth_hdr_t);
    uint16_t payload_len = len - sizeof(eth_hdr_t);
    
    if (type == ETHERNET_TYPE_ARP) {
        if (payload_len >= sizeof(arp_packet_t)) {
            arp_handle_packet((arp_packet_t *)payload);
        }
    } else if (type == ETHERNET_TYPE_IPV4) {
        if (payload_len >= sizeof(ipv4_hdr_t)) {
            ipv4_hdr_t *hdr = (ipv4_hdr_t *)payload;
            uint8_t version = (hdr->version_ihl >> 4) & 0xF;
            if (version == 4) {
                uint16_t ihl = (hdr->version_ihl & 0xF) * 4;
                uint16_t total_len = (hdr->total_length >> 8) | ((hdr->total_length & 0xFF) << 8);
                uint16_t ip_payload_len = total_len - ihl;
                ipv4_handle_packet(hdr, ip_payload_len);
            }
        }
    }
}

// Poll e1000 for incoming packets
void ip_poll(void) {
    uint8_t buffer[2048];
    int len = e1000_recv(buffer);
    if (len > 0) {
        ip_handle_packet(buffer, len);
    }
}

// Send ARP request to resolve IP address (for future use)
static void arp_request(uint32_t target_ip) __attribute__((unused));
static void arp_request(uint32_t target_ip) {
    arp_packet_t arp;
    
    arp.hw_type = ((1 >> 8) & 0xFF) | ((1 & 0xFF) << 8);        // Ethernet
    arp.proto_type = ((ETHERNET_TYPE_IPV4 >> 8) & 0xFF) | ((ETHERNET_TYPE_IPV4 & 0xFF) << 8);
    arp.hw_addr_len = 6;
    arp.proto_addr_len = 4;
    arp.opcode = ((ARP_OPCODE_REQUEST >> 8) & 0xFF) | ((ARP_OPCODE_REQUEST & 0xFF) << 8);
    
    memcpy(arp.src_mac, g_net_config.mac, 6);
    arp.src_ip = ip_to_uint32(g_net_config.local_ip);
    
    memset(arp.dest_mac, 0xFF, 6);  // Broadcast
    arp.dest_ip = target_ip;
    
    printk(T,"[ARP] Requesting ");
    ip_print(uint32_to_ip(target_ip));
    printk("\n");
    
    eth_send((uint8_t *)"\xFF\xFF\xFF\xFF\xFF\xFF", ETHERNET_TYPE_ARP, &arp, sizeof(arp));
}

// DHCP: Send DISCOVER packet
static void dhcp_send_discover(void) {
    dhcp_packet_t dhcp;
    memset(&dhcp, 0, sizeof(dhcp_packet_t));
    
    dhcp.op = 1;              // Client request
    dhcp.htype = 1;           // Ethernet
    dhcp.hlen = 6;
    dhcp.hops = 0;
    dhcp.xid = dhcp_xid;
    dhcp.flags = 0x8000;       // Broadcast flag
    memcpy(dhcp.chaddr, g_net_config.mac, 6);
    dhcp.magic = 0x63825363;   // DHCP magic cookie
    
    // DHCP options
    int opt_idx = 0;
    dhcp.options[opt_idx++] = 53;  // Message type
    dhcp.options[opt_idx++] = 1;
    dhcp.options[opt_idx++] = 1;   // DISCOVER
    dhcp.options[opt_idx++] = 255; // End
    
    printk(T,"[DHCP] Sending DISCOVER (XID: 0x%x)\n", dhcp_xid);
    
    // Send as UDP packet (broadcast to 255.255.255.255:67 from 0.0.0.0:68)
    // For now, just indicate intent
    dhcp_state = 1;
}

// Request DHCP IP
void dhcp_request(void) {
    if (!dhcp_xid)
        dhcp_xid = 0x12345678;
    
    dhcp_send_discover();
}

// Poll DHCP for responses
int dhcp_poll(void) {
    return dhcp_state == 3;  // 1 if bound, 0 otherwise
}

// Initialize IP stack and scan network
void ip_init(void) {
    printk(T,"[IP] Initializing IP networking stack...\n");
    printk(T,"[IP] Version: IPv4\n");
    
    // Print MAC
    printk(T,"[IP] MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n",
           g_net_config.mac[0], g_net_config.mac[1],
           g_net_config.mac[2], g_net_config.mac[3],
           g_net_config.mac[4], g_net_config.mac[5]);
    
    // Clear ARP cache
    memset(arp_cache, 0, sizeof(arp_cache));
    
    // Use default static IP for now
    // TODO: Implement full DHCP client for dynamic assignment
    ip_addr_t default_ip = {192, 168, 1, 100};
    ip_addr_t default_mask = {255, 255, 255, 0};
    ip_addr_t default_gw = {192, 168, 1, 1};
    
    ip_set_static(default_ip, default_mask, default_gw);
    
    printk(T,"[IP] Stack ready - polling e1000 for packets\n");
    g_net_config.configured = 1;
}
