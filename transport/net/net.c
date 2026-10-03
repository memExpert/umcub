/*
 * Minimal IPv4 stack, see net.h.
 */
#include <string.h>
#include "net.h"
#include "umcub_port.h"
#include "umcub_port_eth.h"

#define ETH_HDR      14u
#define IP_HDR       20u
#define UDP_HDR      8u
#define ETYPE_IP     0x0800u
#define ETYPE_ARP    0x0806u
#define PROTO_ICMP   1u
#define PROTO_UDP    17u

net_if_t net_if;
static uint8_t tx[ETH_HDR + NET_MTU];
static uint16_t ip_id;

static const uint8_t bcast_mac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static uint32_t csum_add(uint32_t sum, const uint8_t *p, size_t len)
{
    while (len > 1) {
        sum += net_get16(p);
        p += 2;
        len -= 2;
    }
    if (len) {
        sum += (uint32_t)p[0] << 8;
    }
    return sum;
}

static uint16_t csum_fold(uint32_t sum)
{
    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

void net_init(const uint8_t mac[6])
{
    memset(&net_if, 0, sizeof(net_if));
    memcpy(net_if.mac, mac, 6);
}

static void eth_header(uint8_t *f, const uint8_t dst[6], uint16_t type)
{
    memcpy(f, dst, 6);
    memcpy(f + 6, net_if.mac, 6);
    net_put16(f + 12, type);
}

static size_t ip_header(uint8_t *ip, uint32_t dst, uint8_t proto, size_t payload)
{
    ip[0] = 0x45;
    ip[1] = 0;
    net_put16(ip + 2, (uint16_t)(IP_HDR + payload));
    net_put16(ip + 4, ip_id++);
    net_put16(ip + 6, 0x4000);          /* DF */
    ip[8] = 64;
    ip[9] = proto;
    net_put16(ip + 10, 0);
    net_put32(ip + 12, net_if.configured ? net_if.ip : 0);
    net_put32(ip + 16, dst);
    net_put16(ip + 10, csum_fold(csum_add(0, ip, IP_HDR)));
    return IP_HDR + payload;
}

int net_udp_send(const net_peer_t *to, uint16_t src_port, const uint8_t *data, size_t len)
{
    if (len > NET_UDP_MAX) {
        return UMCUB_EINVAL;
    }
    uint8_t *ip = tx + ETH_HDR;
    uint8_t *udp = ip + IP_HDR;
    eth_header(tx, to->ip == NET_IP_BROADCAST ? bcast_mac : to->mac, ETYPE_IP);
    net_put16(udp, src_port);
    net_put16(udp + 2, to->port);
    net_put16(udp + 4, (uint16_t)(UDP_HDR + len));
    net_put16(udp + 6, 0);
    memmove(udp + UDP_HDR, data, len);
    ip_header(ip, to->ip, PROTO_UDP, UDP_HDR + len);

    /* UDP checksum over the pseudo header */
    uint32_t sum = csum_add(0, ip + 12, 8);
    sum += PROTO_UDP + UDP_HDR + (uint32_t)len;
    uint16_t c = csum_fold(csum_add(sum, udp, UDP_HDR + len));
    net_put16(udp + 6, c ? c : 0xFFFF);

    return umcub_port_eth_tx(tx, ETH_HDR + IP_HDR + UDP_HDR + len);
}

static void arp_input(const uint8_t *f, size_t len)
{
    const uint8_t *a = f + ETH_HDR;
    if (len < ETH_HDR + 28 || net_get16(a) != 1 || net_get16(a + 2) != ETYPE_IP ||
        net_get16(a + 6) != 1 /* request */ || !net_if.configured || net_get32(a + 24) != net_if.ip) {
        return;
    }
    uint8_t *r = tx + ETH_HDR;
    eth_header(tx, a + 8, ETYPE_ARP);
    net_put16(r, 1);
    net_put16(r + 2, ETYPE_IP);
    r[4] = 6;
    r[5] = 4;
    net_put16(r + 6, 2);                /* reply */
    memcpy(r + 8, net_if.mac, 6);
    net_put32(r + 14, net_if.ip);
    memcpy(r + 18, a + 8, 10);          /* target = requester mac + ip */
    (void)umcub_port_eth_tx(tx, ETH_HDR + 28);
}

static void icmp_input(const uint8_t *f, const uint8_t *ip, const uint8_t *icmp, size_t len)
{
    if (len < 8 || icmp[0] != 8 /* echo request */ || len > NET_MTU - IP_HDR ||
        csum_fold(csum_add(0, icmp, len)) != 0) {
        return;
    }
    uint8_t *rip = tx + ETH_HDR;
    uint8_t *ricmp = rip + IP_HDR;
    eth_header(tx, f + 6, ETYPE_IP);
    memmove(ricmp, icmp, len);
    ricmp[0] = 0;                       /* echo reply */
    net_put16(ricmp + 2, 0);
    net_put16(ricmp + 2, csum_fold(csum_add(0, ricmp, len)));
    ip_header(rip, net_get32(ip + 12), PROTO_ICMP, len);
    (void)umcub_port_eth_tx(tx, ETH_HDR + IP_HDR + len);
}

void net_input(const uint8_t *f, size_t len)
{
    if (len < ETH_HDR) {
        return;
    }
    uint16_t type = net_get16(f + 12);
    if (type == ETYPE_ARP) {
        arp_input(f, len);
        return;
    }
    if (type != ETYPE_IP || len < ETH_HDR + IP_HDR) {
        return;
    }
    const uint8_t *ip = f + ETH_HDR;
    size_t ihl = (size_t)(ip[0] & 0xFu) * 4u;
    size_t total = net_get16(ip + 2);
    if ((ip[0] >> 4) != 4 || ihl < IP_HDR || total < ihl || total > len - ETH_HDR ||
        (net_get16(ip + 6) & 0x3FFFu) != 0 /* fragments */ || csum_fold(csum_add(0, ip, ihl)) != 0) {
        return;
    }
    uint32_t dst = net_get32(ip + 16);
    bool for_us = dst == NET_IP_BROADCAST ||
                  (net_if.configured && (dst == net_if.ip || dst == (net_if.ip | ~net_if.netmask)));
    const uint8_t *pl = ip + ihl;
    size_t plen = total - ihl;

    if (ip[9] == PROTO_ICMP && for_us && net_if.configured) {
        icmp_input(f, ip, pl, plen);
    } else if (ip[9] == PROTO_UDP && plen >= UDP_HDR) {
        size_t ulen = net_get16(pl + 4);
        if (ulen < UDP_HDR || ulen > plen) {
            return;
        }
        if (net_get16(pl + 6) != 0) {
            uint32_t sum = csum_add(0, ip + 12, 8) + PROTO_UDP + (uint32_t)ulen;
            if (csum_fold(csum_add(sum, pl, ulen)) != 0) {
                return;
            }
        }
        uint16_t dport = net_get16(pl + 2);
        if (dport == 68) {              /* DHCP client: before we own an address */
            dhcp_input(pl + UDP_HDR, ulen - UDP_HDR);
            return;
        }
        if (!for_us) {
            return;
        }
        net_peer_t from;
        memcpy(from.mac, f + 6, 6);
        from.ip = net_get32(ip + 12);
        from.port = net_get16(pl);
        net_udp_input(&from, dport, pl + UDP_HDR, ulen - UDP_HDR);
    }
}
