#include "ark/types.h"
// ip.c - Real IP networking stack for Ark kernel with e1000 driver

#include "ark/ip.h"
#include "ark/net.h"           /* generic network driver interface */
#include "ark/e1000.h"
#include "ark/printk.h"
#include "ark/mem.h"

#define ETHERNET_TYPE_ARP  0x0806
#define ETHERNET_TYPE_IPV4 0x0800

#define ARP_OPCODE_REQUEST 1
#define ARP_OPCODE_REPLY   2

// Ethernet frame header
typedef struct {
    u8 dest_mac[6];
    u8 src_mac[6];
    u16 type;
} eth_hdr_t;

// ARP packet
typedef struct {
    u16 hw_type;       // Hardware type (1 = Ethernet)
    u16 proto_type;    // Protocol type (0x0800 = IPv4)
    u8 hw_addr_len;    // Hardware address length (6 for Ethernet)
    u8 proto_addr_len; // Protocol address length (4 for IPv4)
    u16 opcode;        // Operation (1 = request, 2 = reply)
    u8 src_mac[6];
    u32 src_ip;
    u8 dest_mac[6];
    u32 dest_ip;
} arp_packet_t;

// Global network configuration
net_config_t g_net_config = {
    .local_ip = {0, 0, 0, 0},
    .netmask = {0, 0, 0, 0},
    .gateway = {0, 0, 0, 0},
    .dns = {0, 0, 0, 0},
    .mac = {0}, /* will be filled by driver */
    .configured = 0
};

// DHCP state machine (not fully implemented)
static int dhcp_state = 0;     // 0=idle, 1=discovering, 2=requesting, 3=bound
static u32 dhcp_xid = 0;
static u32 dhcp_server_ip __attribute__((unused)) = 0;
static u32 dhcp_lease_time __attribute__((unused)) = 0;

// real implementations appear further down in the file (near ip_init)

// ARP cache
#define ARP_CACHE_SIZE 16
static struct {
    u32 ip;
    u8 mac[6];
    int valid;
} arp_cache[ARP_CACHE_SIZE];

// Convert IP address to u32
u32 ip_to_uint32(ip_addr_t ip) {
    return ((u32)ip.a << 24) | ((u32)ip.b << 16) | 
           ((u32)ip.c << 8) | (u32)ip.d;
}

// Convert u32 to IP address
ip_addr_t u32o_ip(u32 val) {
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
void ip_set_mac(u8 *mac) {
    memcpy(g_net_config.mac, mac, 6);
    printk(T,"[IP] MAC address set: %02x:%02x:%02x:%02x:%02x:%02x\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// Calculate IPv4 checksum (for future use)
static u16 ipv4_checksum(void *data, int len) __attribute__((unused));
static u16 ipv4_checksum(void *data, int len) {
    u32 sum = 0;
    u16 *ptr = (u16 *)data;
    
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    if (len)
        sum += *(u8 *)ptr;
    
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    
    return ~sum;
}

// Send ethernet frame via active network driver
static void eth_send(u8 *dest_mac, u16 type, void *payload, u16 payload_len) {
    u8 frame_buf[2048];
    eth_hdr_t *eth = (eth_hdr_t *)frame_buf;
    u8 *frame_payload = frame_buf + sizeof(eth_hdr_t);

    memcpy(eth->dest_mac, dest_mac, 6);
    memcpy(eth->src_mac, g_net_config.mac, 6);
    eth->type = ((type & 0xFF) << 8) | ((type >> 8) & 0xFF);

    memcpy(frame_payload, payload, payload_len);

    net_send(frame_buf, sizeof(eth_hdr_t) + payload_len);

    printk(T,"[ETH] Sent frame to %02x:%02x:%02x:%02x:%02x:%02x (type: 0x%04x)\n",
           dest_mac[0], dest_mac[1], dest_mac[2], dest_mac[3], dest_mac[4], dest_mac[5], type);
}

// Lookup MAC address in ARP cache (for future use)
static int arp_lookup(u32 ip, u8 *mac) __attribute__((unused));
static int arp_lookup(u32 ip, u8 *mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(mac, arp_cache[i].mac, 6);
            return 1;
        }
    }
    return 0;
}

// Add entry to ARP cache
static void arp_cache_add(u32 ip, u8 *mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            arp_cache[i].ip = ip;
            memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].valid = 1;
            printk(T,"[ARP] Cache: ");
            ip_print(u32o_ip(ip));
            printk(" -> %02x:%02x:%02x:%02x:%02x:%02x\n",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            return;
        }
    }
}

// Handle ARP request/reply
static void arp_handle_packet(arp_packet_t *arp) {
    u16 opcode = ((arp->opcode >> 8) & 0xFF) | ((arp->opcode & 0xFF) << 8);
    
    printk(T,"[ARP] Packet: opcode=%d, from ", opcode);
    ip_print(u32o_ip(arp->src_ip));
    printk("\n");
    
    // Cache the sender's MAC/IP mapping
    arp_cache_add(arp->src_ip, arp->src_mac);
    
    // If this is a request for our IP, send a reply
    if (opcode == ARP_OPCODE_REQUEST && 
        arp->dest_ip == ip_to_uint32(g_net_config.local_ip)) {
        printk(T,"[ARP] Sending reply for ");
        ip_print(u32o_ip(arp->dest_ip));
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
static void ipv4_handle_packet(ipv4_hdr_t *hdr, u16 payload_len) {
    u8 protocol = hdr->protocol;
    
    printk(T,"[IP] IPv4 packet: proto=%d, from ", protocol);
    ip_print(u32o_ip(hdr->src_ip));
    printk(" to ");
    ip_print(u32o_ip(hdr->dst_ip));
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
void ip_handle_packet(void *packet, u16 len) {
    if (len < sizeof(eth_hdr_t)) {
        printk(T,"[IP] Packet too short\n");
        return;
    }
    
    eth_hdr_t *eth = (eth_hdr_t *)packet;
    u16 type = ((eth->type >> 8) & 0xFF) | ((eth->type & 0xFF) << 8);
    void *payload = (u8 *)packet + sizeof(eth_hdr_t);
    u16 payload_len = len - sizeof(eth_hdr_t);
    
    if (type == ETHERNET_TYPE_ARP) {
        if (payload_len >= sizeof(arp_packet_t)) {
            arp_handle_packet((arp_packet_t *)payload);
        }
    } else if (type == ETHERNET_TYPE_IPV4) {
        if (payload_len >= sizeof(ipv4_hdr_t)) {
            ipv4_hdr_t *hdr = (ipv4_hdr_t *)payload;
            u8 version = (hdr->version_ihl >> 4) & 0xF;
            if (version == 4) {
                u16 ihl = (hdr->version_ihl & 0xF) * 4;
                u16 total_len = (hdr->total_length >> 8) | ((hdr->total_length & 0xFF) << 8);
                u16 ip_payload_len = total_len - ihl;
                ipv4_handle_packet(hdr, ip_payload_len);
            }
        }
    }
}

// Poll network driver for incoming packets
void ip_poll(void) {
    u8 buffer[2048];
    int len = net_recv(buffer, sizeof(buffer));
    if (len > 0) {
        ip_handle_packet(buffer, len);
    }
}

// Send ARP request to resolve IP address (for future use)
static void arp_request(u32 target_ip) __attribute__((unused));
static void arp_request(u32 target_ip) {
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
    ip_print(u32o_ip(target_ip));
    printk("\n");
    
    eth_send((u8 *)"\xFF\xFF\xFF\xFF\xFF\xFF", ETHERNET_TYPE_ARP, &arp, sizeof(arp));
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

    /* initialize network drivers and pick one */
    net_init_all();

    /* ask driver for MAC address */
    if (net_get_mac(g_net_config.mac) == 0) {
        printk(T,"[IP] MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n",
               g_net_config.mac[0], g_net_config.mac[1],
               g_net_config.mac[2], g_net_config.mac[3],
               g_net_config.mac[4], g_net_config.mac[5]);
    } else {
        printk(T,"[IP] MAC address unknown\n");
    }

    /* Clear ARP cache */
    memset(arp_cache, 0, sizeof(arp_cache));

    /* Do not assign static IP automatically; wait for user or DHCP */
    g_net_config.configured = 0;

    printk(T,"[IP] Stack ready\n");
}
