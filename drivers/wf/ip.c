#include "ark/types.h"
// ip.c — Real IP/UDP/ICMP/DHCP networking stack for Ark kernel

#include "ark/ip.h"
#include "ark/net.h"
#include "ark/printk.h"
#include "ark/mem.h"

/* ── Ethernet constants ─────────────────────────────────────────────── */
#define ETYPE_ARP  0x0806
#define ETYPE_IPV4 0x0800

#define ARP_REQ  1
#define ARP_REP  2

#define IP_PROTO_ICMP  1
#define IP_PROTO_UDP  17

#define ICMP_ECHO_REQUEST  8
#define ICMP_ECHO_REPLY    0

/* ── Packed structures ──────────────────────────────────────────────── */
typedef struct { u8 dst[6]; u8 src[6]; u16 type; } __attribute__((packed)) eth_hdr_t;

typedef struct {
    u16 hw_type; u16 proto_type;
    u8 hw_len;   u8 proto_len;
    u16 opcode;
    u8 src_mac[6]; u32 src_ip;
    u8 dst_mac[6]; u32 dst_ip;
} __attribute__((packed)) arp_pkt_t;

typedef struct {
    u8  ver_ihl; u8 dscp;
    u16 total_len; u16 id;
    u16 frag_off;  u8 ttl; u8 proto;
    u16 checksum;
    u32 src_ip; u32 dst_ip;
} __attribute__((packed)) ip4h_t;

typedef struct {
    u8 type; u8 code; u16 checksum;
    u16 id;  u16 seq;
} __attribute__((packed)) icmp_hdr_t;

typedef struct {
    u16 src_port; u16 dst_port;
    u16 length;   u16 checksum;
} __attribute__((packed)) udp_hdr_t;

/* ── DHCP packet ────────────────────────────────────────────────────── */
typedef struct {
    u8  op,htype,hlen,hops;
    u32 xid;
    u16 secs, flags;
    u32 ciaddr, yiaddr, siaddr, giaddr;
    u8  chaddr[16], sname[64], file[128];
    u32 magic;
    u8  options[308];
} __attribute__((packed)) dhcp_pkt_t;

#define DHCP_MAGIC       0x63825363
#define DHCP_PORT_SERVER 67
#define DHCP_PORT_CLIENT 68

/* ── ARP cache ──────────────────────────────────────────────────────── */
#define ARP_CACHE_SIZE 16
static struct { u32 ip; u8 mac[6]; int valid; } arp_cache[ARP_CACHE_SIZE];

/* ── Global config ──────────────────────────────────────────────────── */
net_config_t g_net_config = { .configured = 0 };

/* ── DHCP state ─────────────────────────────────────────────────────── */
static int  dhcp_state = 0;   /* 0=idle 1=discover 2=request 3=bound */
static u32  dhcp_xid   = 0;
static u32  dhcp_offered_ip    = 0;
static u32  dhcp_server_ip     = 0;

/* ── Utilities ──────────────────────────────────────────────────────── */
u32 ip_to_uint32(ip_addr_t ip) {
    return ((u32)ip.a<<24)|((u32)ip.b<<16)|((u32)ip.c<<8)|(u32)ip.d;
}
ip_addr_t u32o_ip(u32 v) {
    return (ip_addr_t){(v>>24)&0xFF,(v>>16)&0xFF,(v>>8)&0xFF,v&0xFF};
}
const char *ip_print(ip_addr_t ip) {
    static char b[4][16]; static int s=0;
    char *p=b[s++&3],*w=p;
    unsigned pt[4]={ip.a,ip.b,ip.c,ip.d};
    for(int i=0;i<4;i++){
        unsigned v=pt[i];
        if(v>=100)*w++='0'+v/100;
        if(v>=10) *w++='0'+(v/10)%10;
        *w++='0'+v%10;
        if(i<3)*w++='.';
    }
    *w=0; return p;
}
void ip_set_static(ip_addr_t ip,ip_addr_t mask,ip_addr_t gw){
    g_net_config.local_ip=ip; g_net_config.netmask=mask; g_net_config.gateway=gw;
    g_net_config.configured=1;
    printk(T,"ip: static IP=%s mask=%s gw=%s\n",ip_print(ip),ip_print(mask),ip_print(gw));
}
void ip_set_mac(u8 *mac){ memcpy(g_net_config.mac,mac,6); }

/* ── Checksum ───────────────────────────────────────────────────────── */
static u16 checksum(void *data, u32 len) {
    u32 sum=0; u16 *p=(u16*)data;
    while(len>1){ sum+=*p++; len-=2; }
    if(len) sum+=*(u8*)p;
    while(sum>>16) sum=(sum&0xFFFF)+(sum>>16);
    return (u16)(~sum);
}

/* ── Host<->network byte swap helpers ──────────────────────────────── */
static inline u16 htons_(u16 v){ return (u16)((v>>8)|(v<<8)); }
static inline u32 htonl_(u32 v){ return (v>>24)|((v>>8)&0xFF00)|((v<<8)&0xFF0000)|(v<<24); }
#define ntohs_ htons_
#define ntohl_ htonl_

/* ── ARP cache helpers ──────────────────────────────────────────────── */
static void arp_cache_add(u32 ip, u8 *mac){
    for(int i=0;i<ARP_CACHE_SIZE;i++){
        if(!arp_cache[i].valid){
            arp_cache[i].ip=ip; memcpy(arp_cache[i].mac,mac,6); arp_cache[i].valid=1;
            printk(T,"arp: cache %s -> %02x:%02x:%02x:%02x:%02x:%02x\n",
                   ip_print(u32o_ip(ip)),mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
            return;
        }
    }
}
static int arp_lookup(u32 ip, u8 *mac){
    for(int i=0;i<ARP_CACHE_SIZE;i++)
        if(arp_cache[i].valid && arp_cache[i].ip==ip){ memcpy(mac,arp_cache[i].mac,6); return 1; }
    return 0;
}

/* ── Low-level send helpers ─────────────────────────────────────────── */
static void eth_send_raw(u8 *dst_mac, u16 etype, void *payload, u16 plen){
    u8 frame[2048]; eth_hdr_t *eh=(eth_hdr_t*)frame;
    memcpy(eh->dst,dst_mac,6); memcpy(eh->src,g_net_config.mac,6);
    eh->type=htons_(etype);
    memcpy(frame+sizeof(eth_hdr_t),payload,plen);
    net_send(frame,sizeof(eth_hdr_t)+plen);
}

static void send_ipv4(u8 *dst_mac, u8 proto, u32 dst_ip,
                      void *payload, u16 payload_len){
    u8 pkt[2048];
    ip4h_t *ih=(ip4h_t*)pkt;
    ih->ver_ihl   = 0x45;
    ih->dscp      = 0;
    ih->total_len = htons_((u16)(sizeof(ip4h_t)+payload_len));
    ih->id        = 0;
    ih->frag_off  = 0;
    ih->ttl       = 64;
    ih->proto     = proto;
    ih->checksum  = 0;
    ih->src_ip    = htonl_(ip_to_uint32(g_net_config.local_ip));
    ih->dst_ip    = htonl_(dst_ip);
    ih->checksum  = checksum(ih, sizeof(ip4h_t));
    memcpy(pkt+sizeof(ip4h_t), payload, payload_len);
    eth_send_raw(dst_mac, ETYPE_IPV4, pkt, sizeof(ip4h_t)+payload_len);
}

/* ── ARP ────────────────────────────────────────────────────────────── */
static void arp_send_request(u32 target_ip){
    arp_pkt_t a; memset(&a,0,sizeof(a));
    a.hw_type    = htons_(1);
    a.proto_type = htons_(ETYPE_IPV4);
    a.hw_len=6; a.proto_len=4;
    a.opcode     = htons_(ARP_REQ);
    memcpy(a.src_mac, g_net_config.mac, 6);
    a.src_ip = htonl_(ip_to_uint32(g_net_config.local_ip));
    a.dst_ip = htonl_(target_ip);
    u8 bcast[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    eth_send_raw(bcast, ETYPE_ARP, &a, sizeof(a));
    printk(T,"arp: -> %s\n", ip_print(u32o_ip(target_ip)));
}

static void arp_handle(arp_pkt_t *a){
    u16 op = ntohs_(a->opcode);
    /* Byte-swap IPs from network order */
    u32 sip = ntohl_(a->src_ip);
    u32 dip = ntohl_(a->dst_ip);
    arp_cache_add(sip, a->src_mac);
    if(op==ARP_REQ && dip==ip_to_uint32(g_net_config.local_ip)){
        /* Reply */
        arp_pkt_t r; memset(&r,0,sizeof(r));
        r.hw_type    = a->hw_type;
        r.proto_type = a->proto_type;
        r.hw_len=6; r.proto_len=4;
        r.opcode     = htons_(ARP_REP);
        memcpy(r.src_mac,g_net_config.mac,6);
        r.src_ip = a->dst_ip;
        memcpy(r.dst_mac,a->src_mac,6);
        r.dst_ip = a->src_ip;
        eth_send_raw(a->src_mac, ETYPE_ARP, &r, sizeof(r));
        printk(T,"arp: replied to %s\n",ip_print(u32o_ip(sip)));
    }
}

/* ── ICMP ───────────────────────────────────────────────────────────── */
static void icmp_handle(ip4h_t *ih, u8 *data, u16 len){
    if(len<sizeof(icmp_hdr_t)) return;
    icmp_hdr_t *ic=(icmp_hdr_t*)data;
    if(ic->type != ICMP_ECHO_REQUEST) return;

    printk(T,"icmp: ping from %s — sending reply\n",
           ip_print(u32o_ip(ntohl_(ih->src_ip))));

    /* Build echo reply */
    u8 reply[2048];
    icmp_hdr_t *ro=(icmp_hdr_t*)reply;
    ro->type=ICMP_ECHO_REPLY; ro->code=0; ro->checksum=0;
    ro->id=ic->id; ro->seq=ic->seq;
    u16 plen=len; if(plen>sizeof(reply)) plen=(u16)sizeof(reply);
    if(plen>sizeof(icmp_hdr_t))
        memcpy(reply+sizeof(icmp_hdr_t), data+sizeof(icmp_hdr_t), plen-sizeof(icmp_hdr_t));
    ro->checksum=checksum(reply,plen);

    /* Find src MAC: ARP lookup or broadcast */
    u32 sip=ntohl_(ih->src_ip);
    u8 dst_mac[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    arp_lookup(sip, dst_mac);
    send_ipv4(dst_mac, IP_PROTO_ICMP, sip, reply, plen);
}

/* ── UDP send helper ────────────────────────────────────────────────── */
static void udp_send_raw(u8 *dst_mac, u32 dst_ip, u16 sport, u16 dport,
                         void *payload, u16 plen){
    u8 pkt[2048];
    udp_hdr_t *uh=(udp_hdr_t*)pkt;
    uh->src_port=htons_(sport);
    uh->dst_port=htons_(dport);
    uh->length  =htons_((u16)(sizeof(udp_hdr_t)+plen));
    uh->checksum=0; /* optional for IPv4 */
    memcpy(pkt+sizeof(udp_hdr_t), payload, plen);
    send_ipv4(dst_mac, IP_PROTO_UDP, dst_ip, pkt, (u16)(sizeof(udp_hdr_t)+plen));
}

/* ── DHCP ───────────────────────────────────────────────────────────── */
static void dhcp_send_discover(void){
    dhcp_pkt_t d; memset(&d,0,sizeof(d));
    d.op=1; d.htype=1; d.hlen=6;
    d.xid  = htonl_(dhcp_xid);
    d.flags= htons_(0x8000);
    memcpy(d.chaddr, g_net_config.mac, 6);
    d.magic = htonl_(DHCP_MAGIC);
    int i=0;
    d.options[i++]=53; d.options[i++]=1; d.options[i++]=1; /* DHCPDISCOVER */
    d.options[i++]=55; d.options[i++]=3; /* request param list */
    d.options[i++]=1; d.options[i++]=3; d.options[i++]=6;  /* subnet,gw,dns */
    d.options[i++]=255;
    u8 bcast[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    udp_send_raw(bcast, 0xFFFFFFFF, DHCP_PORT_CLIENT, DHCP_PORT_SERVER, &d, sizeof(d));
    dhcp_state=1;
    printk(T,"dhcp: DISCOVER sent (xid=0x%x)\n", dhcp_xid);
}

static void dhcp_send_request(void){
    dhcp_pkt_t d; memset(&d,0,sizeof(d));
    d.op=1; d.htype=1; d.hlen=6;
    d.xid  = htonl_(dhcp_xid);
    d.flags= htons_(0x8000);
    memcpy(d.chaddr, g_net_config.mac, 6);
    d.magic = htonl_(DHCP_MAGIC);
    int i=0;
    d.options[i++]=53; d.options[i++]=1; d.options[i++]=3; /* DHCPREQUEST */
    d.options[i++]=50; d.options[i++]=4; /* requested IP */
    d.options[i++]=(dhcp_offered_ip>>24)&0xFF;
    d.options[i++]=(dhcp_offered_ip>>16)&0xFF;
    d.options[i++]=(dhcp_offered_ip>> 8)&0xFF;
    d.options[i++]=(dhcp_offered_ip    )&0xFF;
    d.options[i++]=54; d.options[i++]=4; /* server ID */
    d.options[i++]=(dhcp_server_ip>>24)&0xFF;
    d.options[i++]=(dhcp_server_ip>>16)&0xFF;
    d.options[i++]=(dhcp_server_ip>> 8)&0xFF;
    d.options[i++]=(dhcp_server_ip    )&0xFF;
    d.options[i++]=255;
    u8 bcast[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    udp_send_raw(bcast, 0xFFFFFFFF, DHCP_PORT_CLIENT, DHCP_PORT_SERVER, &d, sizeof(d));
    dhcp_state=2;
    printk(T,"dhcp: REQUEST sent for %s\n", ip_print(u32o_ip(dhcp_offered_ip)));
}

static void dhcp_parse_options(u8 *opts, u16 len,
    u8 *out_type, u32 *out_mask, u32 *out_gw, u32 *out_dns, u32 *out_sid){
    u16 i=0;
    while(i<len){
        u8 tag=opts[i++];
        if(tag==0) continue;
        if(tag==255) break;
        if(i>=len) break;
        u8 olen=opts[i++];
        if(tag==53 && olen==1) *out_type=opts[i];
        else if(tag==1  && olen==4) *out_mask=((u32)opts[i]<<24)|((u32)opts[i+1]<<16)|((u32)opts[i+2]<<8)|opts[i+3];
        else if(tag==3  && olen>=4) *out_gw  =((u32)opts[i]<<24)|((u32)opts[i+1]<<16)|((u32)opts[i+2]<<8)|opts[i+3];
        else if(tag==6  && olen>=4) *out_dns =((u32)opts[i]<<24)|((u32)opts[i+1]<<16)|((u32)opts[i+2]<<8)|opts[i+3];
        else if(tag==54 && olen==4) *out_sid =((u32)opts[i]<<24)|((u32)opts[i+1]<<16)|((u32)opts[i+2]<<8)|opts[i+3];
        i+=olen;
    }
}

static void dhcp_handle(udp_hdr_t *uh, u8 *data, u16 len){
    (void)uh;
    if(len < sizeof(dhcp_pkt_t)) return;
    dhcp_pkt_t *d=(dhcp_pkt_t*)data;
    if(ntohl_(d->xid) != dhcp_xid) return;
    if(ntohl_(d->magic) != DHCP_MAGIC) return;

    u8 mtype=0; u32 mask=0,gw=0,dns=0,sid=0;
    dhcp_parse_options(d->options, (u16)(len - (u32)((u8*)d->options - data)),
                       &mtype, &mask, &gw, &dns, &sid);

    u32 offered = ntohl_(d->yiaddr);

    if(mtype==2 && dhcp_state==1){ /* OFFER */
        dhcp_offered_ip = offered;
        dhcp_server_ip  = sid;
        printk(T,"dhcp: OFFER %s\n", ip_print(u32o_ip(offered)));
        dhcp_send_request();
    } else if(mtype==5 && dhcp_state==2){ /* ACK */
        g_net_config.local_ip  = u32o_ip(offered);
        g_net_config.netmask   = u32o_ip(mask ? mask : 0xFFFFFF00);
        g_net_config.gateway   = u32o_ip(gw);
        g_net_config.dns       = u32o_ip(dns);
        g_net_config.configured = 1;
        dhcp_state = 3;
        printk(T,"dhcp: ACK — IP=%s mask=%s gw=%s dns=%s\n",
               ip_print(g_net_config.local_ip), ip_print(g_net_config.netmask),
               ip_print(g_net_config.gateway),  ip_print(g_net_config.dns));
        /* Announce ourselves with a gratuitous ARP */
        arp_send_request(ip_to_uint32(g_net_config.local_ip));
    } else if(mtype==6){ /* NAK */
        printk(T,"dhcp: NAK — restarting\n");
        dhcp_state=0;
    }
}

/* ── UDP dispatch ───────────────────────────────────────────────────── */
static void udp_handle(ip4h_t *ih, u8 *data, u16 len){
    if(len < sizeof(udp_hdr_t)) return;
    udp_hdr_t *uh=(udp_hdr_t*)data;
    u16 dport = ntohs_(uh->dst_port);
    u16 plen  = ntohs_(uh->length);
    if(plen < sizeof(udp_hdr_t)) return;
    u16 paylen=(u16)(plen - sizeof(udp_hdr_t));
    u8 *payload = data + sizeof(udp_hdr_t);
    if(dport == DHCP_PORT_CLIENT)
        dhcp_handle(uh, payload, paylen);
    else
        printk(T,"udp: port %d -> %d len=%d\n", ntohs_(uh->src_port), dport, (u32)paylen);
    (void)ih;
}

/* ── IPv4 dispatch ──────────────────────────────────────────────────── */
static void ipv4_handle(ip4h_t *ih, u16 frame_payload_len){
    u32 my_ip = ip_to_uint32(g_net_config.local_ip);
    u32 dst    = ntohl_(ih->dst_ip);
    /* Accept unicast to us, broadcast, or pre-DHCP (0.0.0.0) */
    if(dst != my_ip && dst != 0xFFFFFFFF && my_ip != 0) return;

    u8 ihl = (ih->ver_ihl & 0xF) * 4;
    u8 *payload = (u8*)ih + ihl;
    u16 total   = ntohs_(ih->total_len);
    u16 paylen  = (total > ihl) ? (u16)(total - ihl) : 0;
    if(paylen > frame_payload_len - ihl) paylen = (u16)(frame_payload_len - ihl);

    switch(ih->proto){
    case IP_PROTO_ICMP: icmp_handle(ih, payload, paylen); break;
    case IP_PROTO_UDP:  udp_handle(ih, payload, paylen);  break;
    default: break;
    }
}

/* ── Main packet entry ──────────────────────────────────────────────── */
void ip_handle_packet(void *packet, u16 len){
    if(len < (u16)sizeof(eth_hdr_t)) return;
    eth_hdr_t *eh=(eth_hdr_t*)packet;
    u16 etype = ntohs_(eh->type);
    void *payload = (u8*)packet + sizeof(eth_hdr_t);
    u16 plen  = (u16)(len - sizeof(eth_hdr_t));

    if(etype==ETYPE_ARP && plen>=(u16)sizeof(arp_pkt_t))
        arp_handle((arp_pkt_t*)payload);
    else if(etype==ETYPE_IPV4 && plen>=(u16)sizeof(ip4h_t))
        ipv4_handle((ip4h_t*)payload, plen);
}

void ip_poll(void){
    u8 buf[2048];
    int n = net_recv(buf, sizeof(buf));
    if(n > 0) ip_handle_packet(buf, (u16)n);
}

/* ── DHCP public API ────────────────────────────────────────────────── */
void dhcp_request(void){
    if(!dhcp_xid) dhcp_xid = 0xAB1234CD;
    dhcp_state = 0;
    dhcp_send_discover();
}
int dhcp_poll(void){ return dhcp_state == 3; }

/* ── ip_init ────────────────────────────────────────────────────────── */
void ip_init(void){
    printk(T,"ip: initializing stack\n");
    net_init_all();
    if(net_get_mac(g_net_config.mac) == 0)
        printk(T,"ip: MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
               g_net_config.mac[0], g_net_config.mac[1], g_net_config.mac[2],
               g_net_config.mac[3], g_net_config.mac[4], g_net_config.mac[5]);
    memset(arp_cache, 0, sizeof(arp_cache));
    g_net_config.configured = 0;
    printk(T,"ip: stack ready\n");
}

/* Expose raw config pointer for syscall layer */
void *get_net_config_ptr(void) { return &g_net_config; }

