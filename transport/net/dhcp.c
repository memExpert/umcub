/*
 * DHCP client (RFC 2131 subset): DISCOVER -> OFFER -> REQUEST -> ACK,
 * renewal at T1 by broadcast REQUEST, exponential retransmission, and a
 * static fallback address after UMCUB_CFG_ETH_DHCP_TIMEOUT_MS (DHCP keeps
 * running in the background and takes over when a lease arrives).
 */
#include <string.h>
#include "net.h"
#include "umcub_cfg.h"
#include "umcub_log.h"
#include "umcub_port.h"
#include "umcub_handoff.h"

#define BOOTP_LEN     236u
#define COOKIE        0x63825363u
#define MSG_DISCOVER  1
#define MSG_OFFER     2
#define MSG_REQUEST   3
#define MSG_ACK       5
#define MSG_NAK       6

enum { ST_OFF, ST_SELECTING, ST_REQUESTING, ST_BOUND, ST_RENEWING };

static uint8_t state;
static uint32_t xid;
static uint32_t offered_ip, server_id, lease_s;
static uint32_t next_tx, retry_ms, started, bound_at;
static bool fallback_applied;
static uint8_t pkt[300];

static void log_ip(const char *what, uint32_t ip)
{
    UMCUB_LOG_INF("eth: %s %lu.%lu.%lu.%lu", what, (unsigned long)(ip >> 24), (unsigned long)((ip >> 16) & 255u),
                  (unsigned long)((ip >> 8) & 255u), (unsigned long)(ip & 255u));
}

static void send_msg(uint8_t type)
{
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 1;                                   /* BOOTREQUEST */
    pkt[1] = 1;                                   /* ethernet */
    pkt[2] = 6;
    net_put32(pkt + 4, xid);
    net_put16(pkt + 10, 0x8000);                  /* broadcast replies */
    if (state == ST_RENEWING) {
        net_put32(pkt + 12, net_if.ip);           /* ciaddr */
    }
    memcpy(pkt + 28, net_if.mac, 6);
    net_put32(pkt + BOOTP_LEN, COOKIE);

    uint8_t *o = pkt + BOOTP_LEN + 4;
    *o++ = 53; *o++ = 1; *o++ = type;
    if (type == MSG_REQUEST && state == ST_REQUESTING) {
        *o++ = 50; *o++ = 4; net_put32(o, offered_ip); o += 4;
        *o++ = 54; *o++ = 4; net_put32(o, server_id); o += 4;
    }
    static const char host[] = "umcub-";
    *o++ = 12; *o++ = 10;
    memcpy(o, host, 6);
    for (int i = 0; i < 4; i++) {
        o[6 + i] = "0123456789abcdef"[(net_if.mac[4 + i / 2] >> (i % 2 ? 0 : 4)) & 15];
    }
    o += 10;
    *o++ = 55; *o++ = 4; *o++ = 1; *o++ = 3; *o++ = 51; *o++ = 54;
    *o++ = 255;

    net_peer_t to = { .ip = NET_IP_BROADCAST, .port = 67 };
    (void)net_udp_send(&to, 68, pkt, sizeof(pkt));
}

static void schedule_retry(void)
{
    next_tx = umcub_port_millis() + retry_ms;
    if (retry_ms < 16000u) {
        retry_ms *= 2u;
    }
}

void dhcp_start(void)
{
#if UMCUB_CFG_ETH_DHCP
    uint8_t uid[12];
    umcub_port_uid(uid);
    xid = umcub_crc32(uid, sizeof(uid)) ^ umcub_port_millis();
    state = ST_SELECTING;
    retry_ms = 2000;
    started = umcub_port_millis();
    next_tx = started;
#else
    net_if.ip = UMCUB_CFG_ETH_IP;
    net_if.netmask = UMCUB_CFG_ETH_NETMASK;
    net_if.gateway = UMCUB_CFG_ETH_GATEWAY;
    net_if.configured = true;
    log_ip("static ip", net_if.ip);
#endif
}

void dhcp_tick(void)
{
#if UMCUB_CFG_ETH_DHCP
    uint32_t now = umcub_port_millis();
    if (!fallback_applied && state != ST_BOUND && state != ST_RENEWING &&
        (uint32_t)(now - started) > UMCUB_CFG_ETH_DHCP_TIMEOUT_MS) {
        fallback_applied = true;
        net_if.ip = UMCUB_CFG_ETH_IP;
        net_if.netmask = UMCUB_CFG_ETH_NETMASK;
        net_if.gateway = UMCUB_CFG_ETH_GATEWAY;
        net_if.configured = true;
        log_ip("no DHCP answer, fallback ip", net_if.ip);
    }
    if (state == ST_BOUND && (uint32_t)(now - bound_at) / 1000u >= lease_s / 2u) {
        state = ST_RENEWING;
        retry_ms = 2000;
        next_tx = now;
    }
    if (state != ST_BOUND && (int32_t)(now - next_tx) >= 0) {
        if (state == ST_RENEWING && (uint32_t)(now - bound_at) / 1000u >= lease_s) {
            state = ST_SELECTING;           /* lease expired */
            retry_ms = 2000;
        }
        send_msg(state == ST_SELECTING ? MSG_DISCOVER : MSG_REQUEST);
        schedule_retry();
    }
#endif
}

void dhcp_input(const uint8_t *d, size_t len)
{
#if UMCUB_CFG_ETH_DHCP
    if (len < BOOTP_LEN + 4 || d[0] != 2 || net_get32(d + 4) != xid ||
        memcmp(d + 28, net_if.mac, 6) != 0 || net_get32(d + BOOTP_LEN) != COOKIE) {
        return;
    }
    uint8_t type = 0;
    uint32_t sid = 0, mask = 0, router = 0, lease = 3600;
    for (size_t i = BOOTP_LEN + 4; i < len;) {
        uint8_t opt = d[i];
        if (opt == 255) {
            break;
        }
        if (opt == 0) {
            i++;
            continue;
        }
        if (i + 1 >= len || i + 2 + d[i + 1] > len) {
            return;
        }
        const uint8_t *v = d + i + 2;
        uint8_t l = d[i + 1];
        if (opt == 53 && l == 1) type = v[0];
        else if (opt == 54 && l == 4) sid = net_get32(v);
        else if (opt == 1 && l == 4) mask = net_get32(v);
        else if (opt == 3 && l >= 4) router = net_get32(v);
        else if (opt == 51 && l == 4) lease = net_get32(v);
        i += 2u + l;
    }
    uint32_t yiaddr = net_get32(d + 16);

    if (type == MSG_OFFER && state == ST_SELECTING && yiaddr) {
        offered_ip = yiaddr;
        server_id = sid;
        state = ST_REQUESTING;
        retry_ms = 2000;
        send_msg(MSG_REQUEST);
        schedule_retry();
    } else if (type == MSG_ACK && (state == ST_REQUESTING || state == ST_RENEWING) && yiaddr) {
        bool changed = !net_if.configured || net_if.ip != yiaddr;
        net_if.ip = yiaddr;
        net_if.netmask = mask ? mask : 0xFFFFFF00u;
        net_if.gateway = router;
        net_if.configured = true;
        lease_s = lease < 60u ? 60u : lease;
        bound_at = umcub_port_millis();
        state = ST_BOUND;
        if (changed) {
            log_ip("DHCP lease", yiaddr);
        }
    } else if (type == MSG_NAK && state != ST_SELECTING) {
        state = ST_SELECTING;
        retry_ms = 2000;
        next_tx = umcub_port_millis();
    }
#else
    (void)d;
    (void)len;
#endif
}
