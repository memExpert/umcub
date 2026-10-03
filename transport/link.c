/*
 * umcub link (see umcub_link.h): framing, addressing, discovery.
 */
#include <string.h>
#include "umcub_cfg.h"
#include "umcub_boot.h"
#include "umcub_link.h"
#include "umcub_port.h"
#include "umcub_random.h"
#include "umcub_version.h"
#include "base64/base64.h"

#define MAX_T           (UMCUB_TRANSPORT_MAX ? UMCUB_TRANSPORT_MAX : 1)
#define ANNOUNCE_LEN    24u
#define UID_LEN         12u

struct link_state {
    uint16_t peer;              /* address of the host that talked to us last */
    uint32_t tx_seq;
    bool selected;              /* unassigned node selected by UID (HELLO + FLAG_UID) */
    bool announce_pending;
    uint32_t announce_at;
    uint16_t announce_to;
};

static struct link_state st[MAX_T];
static uint8_t frame_buf[UMCUB_LINK_FRAME_MAX];
static char line_buf[UMCUB_LINK_LINE_MAX + 1];
static bool wakeup;

static int index_of(const umcub_transport_t *t)
{
    for (unsigned i = 0; i < umcub_transport_count && i < MAX_T; i++) {
        if (umcub_transports[i] == t) {
            return (int)i;
        }
    }
    return -1;
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

/* Build a frame around `payload` in frame_buf and hand it to the transport. */
static void send_frame(const umcub_transport_t *t, struct link_state *s, uint8_t type, uint16_t dst,
                       const uint8_t *payload, size_t len)
{
    if (len > UMCUB_CFG_SMP_MTU) {
        return;
    }
    uint8_t *f = frame_buf;
    f[0] = UMCUB_LINK_MAGIC;
    f[1] = UMCUB_LINK_VERSION;
    f[2] = type;
    f[3] = 0;
    put16(f + 4, dst);
    put16(f + 6, umcub_node_address());
    put32(f + 8, s->tx_seq++);
    put16(f + 12, (uint16_t)len);
    if (len && payload != f + UMCUB_LINK_HDR) {
        memmove(f + UMCUB_LINK_HDR, payload, len);
    }
    size_t n = UMCUB_LINK_HDR + len;

    if (t->send_packet && !(t->read && t->write)) {
        (void)t->send_packet(f, n);
        return;
    }
    if (t->write) {
        line_buf[0] = UMCUB_LINK_LINE_START1;
        line_buf[1] = UMCUB_LINK_LINE_START2;
        int b = base64_encode(f, (int)n, &line_buf[2], 1);
        line_buf[2 + b] = '\n';
        t->write((const uint8_t *)line_buf, (size_t)b + 3u);
    }
}

static void announce(const umcub_transport_t *t, struct link_state *s, uint16_t to)
{
    uint8_t p[ANNOUNCE_LEN];
    umcub_port_uid(p);                              /* 0..11 UID */
    put16(p + 12, umcub_node_address());            /* 12 node address */
    put32(p + 14, UMCUB_CFG_BOARD_TYPE);            /* 14 board type */
    put16(p + 18, UMCUB_CFG_BOARD_REV);             /* 18 board revision */
    p[20] = t->link;                                /* 20 UMCUB_LINK_* of this transport */
    p[21] = UMCUB_VERSION_MAJOR;                    /* 21..23 bootloader version */
    p[22] = UMCUB_VERSION_MINOR;
    p[23] = UMCUB_VERSION_PATCH;
    send_frame(t, s, UMCUB_LT_ANNOUNCE, to, p, sizeof(p));
}

/* DISCOVER: answer in a random slot unless our UID is in the "already found"
 * list, so that several unknown devices do not answer at the same time. */
static void discover(struct link_state *s, uint16_t from, const uint8_t *p, size_t len)
{
    if (len < 3) {
        return;
    }
    uint8_t slots = p[0] ? p[0] : 1;
    uint16_t slot_ms = get16(p + 1);
    uint8_t uid[UID_LEN];
    umcub_port_uid(uid);
    for (size_t off = 3; off + UID_LEN <= len; off += UID_LEN) {
        if (memcmp(p + off, uid, UID_LEN) == 0) {
            return;                                 /* host knows us already */
        }
    }
    s->announce_pending = true;
    s->announce_at = umcub_port_millis() + (umcub_random_u32() % slots) * slot_ms;
    s->announce_to = from;
}

void umcub_link_rx(const umcub_transport_t *t, const uint8_t *f, size_t len)
{
    int i = index_of(t);
    if (i < 0 || len < UMCUB_LINK_HDR || f[0] != UMCUB_LINK_MAGIC || f[1] != UMCUB_LINK_VERSION) {
        return;
    }
    struct link_state *s = &st[i];
    uint8_t type = f[2], flags = f[3];
    uint16_t dst = get16(f + 4), src = get16(f + 6);
    size_t plen = get16(f + 12);
    size_t need = UMCUB_LINK_HDR + plen + ((flags & UMCUB_LF_MAC) ? UMCUB_LINK_TAG : 0u);
    if (plen > UMCUB_CFG_SMP_MTU || need != len) {
        return;                                     /* truncated or oversized */
    }
    const uint8_t *p = f + UMCUB_LINK_HDR;
    uint16_t me = umcub_node_address();

    if (type == UMCUB_LT_DISCOVER) {
        if (dst == UMCUB_LINK_BROADCAST) {
            discover(s, src, p, plen);
        }
        return;
    }
    if (type == UMCUB_LT_HELLO && (flags & UMCUB_LF_UID)) {
        uint8_t uid[UID_LEN];
        umcub_port_uid(uid);
        s->selected = plen >= UID_LEN && memcmp(p, uid, UID_LEN) == 0;
        if (!s->selected) {
            return;
        }
    } else if (type == UMCUB_LT_HELLO) {
        s->selected = false;                        /* the host switched to an address */
        if (dst != me || me == 0) {
            return;
        }
    } else if (!((me != 0 && dst == me) || (dst == 0 && s->selected))) {
        return;                                     /* for another node */
    }

    s->peer = src;
    wakeup = true;
    switch (type) {
    case UMCUB_LT_HELLO:
        announce(t, s, src);
        break;
    case UMCUB_LT_DATA:
        (void)umcub_mux_link_rx(t, p, plen);       /* busy: dropped, the host retries */
        break;
    default:
        break;                                      /* SECURE types: stage of umcub link */
    }
}

void umcub_link_rx_line(const umcub_transport_t *t, const char *b64, size_t len)
{
    if (len == 0 || len > UMCUB_LINK_LINE_MAX - 3u || (len * 3u) / 4u > sizeof(frame_buf)) {
        return;
    }
    memcpy(line_buf, b64, len);
    line_buf[len] = '\0';
    int n = base64_decode(line_buf, frame_buf);
    if (n > 0) {
        umcub_link_rx(t, frame_buf, (size_t)n);
    }
}

void umcub_link_send_data(const umcub_transport_t *t, const uint8_t *data, size_t len)
{
    int i = index_of(t);
    if (i >= 0) {
        send_frame(t, &st[i], UMCUB_LT_DATA, st[i].peer, data, len);
    }
}

void umcub_link_poll(void)
{
    uint32_t now = umcub_port_millis();
    for (unsigned i = 0; i < umcub_transport_count && i < MAX_T; i++) {
        struct link_state *s = &st[i];
        if (s->announce_pending && (int32_t)(now - s->announce_at) >= 0) {
            s->announce_pending = false;
            announce(umcub_transports[i], s, s->announce_to);
        }
    }
}

bool umcub_link_take_wakeup(void)
{
    bool w = wakeup;
    wakeup = false;
    return w;
}
