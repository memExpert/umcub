/*
 * Minimal IPv4 stack for the bootloader: Ethernet II, ARP (answers only),
 * IPv4 (no fragments, no options), ICMP echo, UDP. Replies always go to the
 * sender's MAC address, so no ARP cache or routing table is needed.
 */
#ifndef UMCUB_NET_H
#define UMCUB_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NET_MTU          1500u
#define NET_UDP_MAX      (NET_MTU - 20u - 8u)
#define NET_IP_BROADCAST 0xFFFFFFFFu

typedef struct {
    uint8_t mac[6];
    uint32_t ip;            /* host byte order */
    uint16_t port;
} net_peer_t;

typedef struct {
    uint8_t mac[6];
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    bool configured;        /* ip is valid (DHCP lease or static fallback) */
} net_if_t;

extern net_if_t net_if;

void net_init(const uint8_t mac[6]);
void net_input(const uint8_t *frame, size_t len);
int net_udp_send(const net_peer_t *to, uint16_t src_port, const uint8_t *data, size_t len);

/* Upper layers (dhcp.c, net_transport.c). */
void net_udp_input(const net_peer_t *from, uint16_t dst_port, const uint8_t *data, size_t len);
void dhcp_start(void);
void dhcp_tick(void);
void dhcp_input(const uint8_t *data, size_t len);

static inline uint16_t net_get16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t net_get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline void net_put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void net_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

#endif
