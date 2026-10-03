/*
 * umcub link (see umcub_link.h): framing, addressing, discovery and, on
 * SECURE transports, host authentication and authenticated sessions.
 */
#include <string.h>
#include "umcub_cfg.h"
#include "umcub_boot.h"
#include "umcub_link.h"
#include "umcub_port.h"
#include "umcub_random.h"
#include "umcub_version.h"
#include "base64/base64.h"
#if UMCUB_CFG_LINK_SECURE_ANY
#include "tinycrypt/constants.h"
#include "tinycrypt/ecc.h"
#include "tinycrypt/ecc_dh.h"
#include "tinycrypt/ecc_dsa.h"
#include "tinycrypt/hmac.h"
#include "tinycrypt/sha256.h"
#include "tinycrypt/utils.h"
#if UMCUB_CFG_LINK_ENCRYPT
#include "tinycrypt/aes.h"
#include "tinycrypt/ctr_mode.h"
#endif
#endif

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

#if UMCUB_CFG_LINK_SECURE_ANY
#define NONCE_LEN       32u
#define PUB_LEN         64u
#define SIG_LEN         64u
#define CHALLENGE_LEN   (ANNOUNCE_LEN + NONCE_LEN)
#define AUTH_LEN        (PUB_LEN + NONCE_LEN + SIG_LEN)
#define KEY_MAC_H2D     0u          /* offsets in session.keys */
#define KEY_MAC_D2H     32u
#define KEY_ENC_H2D     64u
#define KEY_ENC_D2H     80u
#define LABEL           "umcub-link-v1"

/* tools/umcub_keys.py output (CMake UMCUB_DEVICE_KEY / UMCUB_HOST_KEY). */
extern const uint8_t umcub_link_device_priv[32];
extern const uint8_t umcub_link_admin_pub[PUB_LEN];

/* The open challenge (one AUTH) and the session: one host at a time. */
static struct {
    int8_t t;                       /* transport index, -1 = none */
    uint16_t peer;
    uint8_t payload[CHALLENGE_LEN];
} chal = { .t = -1 };
static struct {
    int8_t t;
    uint16_t peer;
    uint32_t rx_seq;                /* last accepted host seq */
    uint32_t last_ms;
    uint8_t keys[96];
} sess = { .t = -1 };
#if UMCUB_CFG_LINK_ENCRYPT
static uint8_t plain_buf[UMCUB_CFG_SMP_MTU];
#endif
#endif

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

#if UMCUB_CFG_LINK_SECURE_ANY
/* HMAC-SHA256 over up to three parts. */
static void hmac3(uint8_t out[TC_SHA256_DIGEST_SIZE], const uint8_t *key, size_t klen,
                  const void *a, size_t alen, const void *b, size_t blen, const void *c, size_t clen)
{
    struct tc_hmac_state_struct h;
    (void)tc_hmac_set_key(&h, key, (unsigned)klen);
    (void)tc_hmac_init(&h);
    (void)tc_hmac_update(&h, a, (unsigned)alen);
    if (blen) {
        (void)tc_hmac_update(&h, b, (unsigned)blen);
    }
    if (clen) {
        (void)tc_hmac_update(&h, c, (unsigned)clen);
    }
    (void)tc_hmac_final(out, TC_SHA256_DIGEST_SIZE, &h);
    memset(&h, 0, sizeof(h));
}

/* Tag of the frame f (header + payload, `n` bytes) in direction `key_off`. */
static void frame_tag(uint8_t tag[UMCUB_LINK_TAG], const uint8_t *f, size_t n, unsigned key_off)
{
    uint8_t d[TC_SHA256_DIGEST_SIZE];
    hmac3(d, &sess.keys[key_off], 32, f, n, NULL, 0, NULL, 0);
    memcpy(tag, d, UMCUB_LINK_TAG);
}

#if UMCUB_CFG_LINK_ENCRYPT
/* AES-128-CTR, counter block seq u32 LE | 0 x 8 | block u32 BE from 0. */
static void ctr_crypt(uint8_t *out, const uint8_t *in, size_t len, uint32_t seq, unsigned key_off)
{
    struct tc_aes_key_sched_struct ks;
    uint8_t ctr[16] = { 0 };
    uint32_t off = 0;
    put32(ctr, seq);
    (void)tc_aes128_set_encrypt_key(&ks, &sess.keys[key_off]);
    (void)tc_ctr_mode(out, (unsigned)len, in, (unsigned)len, ctr, &off, &ks);
    memset(&ks, 0, sizeof(ks));
}
#endif
#endif

/* Build a frame around `payload` in frame_buf and hand it to the transport.
 * On a SECURE transport every frame but ANNOUNCE / CHALLENGE belongs to the
 * session: DATA payload encrypted (UMCUB_CFG_LINK_ENCRYPT), tag appended. */
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
#if UMCUB_CFG_LINK_SECURE_ANY
    if (t->link == UMCUB_LINK_SECURE && type != UMCUB_LT_ANNOUNCE && type != UMCUB_LT_CHALLENGE) {
        f[3] = UMCUB_LF_MAC;
#if UMCUB_CFG_LINK_ENCRYPT
        if (type == UMCUB_LT_DATA && len) {
            f[3] |= UMCUB_LF_ENC;
            ctr_crypt(f + UMCUB_LINK_HDR, f + UMCUB_LINK_HDR, len, s->tx_seq - 1u, KEY_ENC_D2H);
        }
#endif
        frame_tag(f + n, f, n, KEY_MAC_D2H);
        n += UMCUB_LINK_TAG;
    }
#endif

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

/* SECURE transport that must stay closed: readout protection is off. */
static bool secure_closed(const umcub_transport_t *t)
{
#if UMCUB_CFG_LINK_SECURE_ANY && UMCUB_CFG_LINK_REQUIRE_RDP
    return t->link == UMCUB_LINK_SECURE && umcub_port_rdp_level() == 0;
#else
    (void)t;
    return false;
#endif
}

static void identity(const umcub_transport_t *t, uint8_t *p)
{
    umcub_port_uid(p);                              /* 0..11 UID */
    put16(p + 12, umcub_node_address());            /* 12 node address */
    put32(p + 14, UMCUB_CFG_BOARD_TYPE);            /* 14 board type */
    put16(p + 18, UMCUB_CFG_BOARD_REV);             /* 18 board revision */
    p[20] = t->link;                                /* 20 UMCUB_LINK_* of this transport + flags */
    if (t->link == UMCUB_LINK_SECURE && UMCUB_CFG_LINK_ENCRYPT) {
        p[20] |= UMCUB_LINK_ANNOUNCE_ENC;
    }
    if (secure_closed(t)) {
        p[20] |= UMCUB_LINK_ANNOUNCE_CLOSED;
    }
    p[21] = UMCUB_VERSION_MAJOR;                    /* 21..23 bootloader version */
    p[22] = UMCUB_VERSION_MINOR;
    p[23] = UMCUB_VERSION_PATCH;
}

static void announce(const umcub_transport_t *t, struct link_state *s, uint16_t to)
{
    uint8_t p[ANNOUNCE_LEN];
    identity(t, p);
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

#if UMCUB_CFG_LINK_SECURE_ANY
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)get16(p) | (uint32_t)get16(p + 2) << 16;
}

static void end_session(void)
{
    memset(&sess, 0, sizeof(sess));
    sess.t = -1;
}

/* AUTH payload against the open challenge: admin signature, then ECDH with
 * the device key and the session keys. */
static bool auth(const uint8_t *p, uint16_t host, uint8_t keys[96])
{
    const uint8_t *eph = p, *sig = p + PUB_LEN + NONCE_LEN;     /* eph_pub | nonce_h | sig */
    uECC_Curve curve = uECC_secp256r1();
    uint8_t th[TC_SHA256_DIGEST_SIZE], h2[2];
    struct tc_sha256_state_struct h;
    put16(h2, host);
    (void)tc_sha256_init(&h);
    (void)tc_sha256_update(&h, (const uint8_t *)LABEL, sizeof(LABEL) - 1u);
    (void)tc_sha256_update(&h, h2, sizeof(h2));
    (void)tc_sha256_update(&h, chal.payload, CHALLENGE_LEN);
    (void)tc_sha256_update(&h, eph, PUB_LEN + NONCE_LEN);  /* eph_pub | nonce_h */
    (void)tc_sha256_final(th, &h);

    if (uECC_verify(umcub_link_admin_pub, th, sizeof(th), sig, curve) != TC_CRYPTO_SUCCESS) {
        return false;
    }
    umcub_port_wdg_feed();                          /* ~0.65 s each on a 72 MHz Cortex-M3 */
    if (uECC_valid_public_key(eph, curve) != 0) {
        return false;
    }
    uint8_t shared[32];
    if (!uECC_shared_secret(eph, umcub_link_device_priv, shared, curve)) {
        return false;
    }
    umcub_port_wdg_feed();

    /* HKDF-SHA256: extract with th as salt, expand to 96 bytes. */
    static const char info[] = LABEL " keys";
    uint8_t prk[TC_SHA256_DIGEST_SIZE];
    hmac3(prk, th, sizeof(th), shared, sizeof(shared), NULL, 0, NULL, 0);
    for (uint8_t n = 1; n <= 3; n++) {
        uint8_t *out = &keys[(n - 1u) * 32u];
        if (n == 1) {
            hmac3(out, prk, sizeof(prk), info, sizeof(info) - 1u, &n, 1, NULL, 0);
        } else {
            hmac3(out, prk, sizeof(prk), out - 32, 32, info, sizeof(info) - 1u, &n, 1);
        }
    }
    memset(shared, 0, sizeof(shared));
    memset(prk, 0, sizeof(prk));
    return true;
}

/* A frame for this node on a SECURE transport (addressing already checked). */
static void secure_rx(const umcub_transport_t *t, int i, struct link_state *s, const uint8_t *f, size_t len)
{
    uint8_t type = f[2], flags = f[3];
    uint16_t src = get16(f + 6);
    uint32_t seq = get32(f + 8);
    size_t plen = get16(f + 12);
    const uint8_t *p = f + UMCUB_LINK_HDR;
    uint32_t now = umcub_port_millis();

    if (sess.t >= 0 && now - sess.last_ms > UMCUB_CFG_LINK_SESSION_MS) {
        end_session();
    }
    switch (type) {
    case UMCUB_LT_HELLO:
        if (secure_closed(t)) {
            announce(t, s, src);                    /* identity only, flagged closed */
            return;
        }
        identity(t, chal.payload);
        umcub_random(chal.payload + ANNOUNCE_LEN, NONCE_LEN);
        chal.t = (int8_t)i;
        chal.peer = src;
        send_frame(t, s, UMCUB_LT_CHALLENGE, src, chal.payload, CHALLENGE_LEN);
        return;
    case UMCUB_LT_AUTH: {
        if (chal.t != i || chal.peer != src || plen != AUTH_LEN || flags != 0) {
            return;
        }
        chal.t = -1;                                /* one attempt per challenge */
        uint8_t keys[sizeof(sess.keys)];
        bool ok = auth(p, src, keys);
        if (ok) {                                   /* a failed attempt leaves a session alone */
            memcpy(sess.keys, keys, sizeof(keys));
        }
        memset(keys, 0, sizeof(keys));
        if (!ok) {
            return;
        }
        sess.t = (int8_t)i;
        sess.peer = src;
        sess.rx_seq = seq;
        sess.last_ms = now;
        s->peer = src;
        wakeup = true;
        send_frame(t, s, UMCUB_LT_AUTH_OK, src, NULL, 0);
        return;
    }
    case UMCUB_LT_DATA:
    case UMCUB_LT_CLOSE:
        break;
    default:
        return;
    }

    uint8_t want = UMCUB_LF_MAC;
#if UMCUB_CFG_LINK_ENCRYPT
    if (type == UMCUB_LT_DATA && plen) {
        want |= UMCUB_LF_ENC;
    }
#endif
    uint8_t tag[UMCUB_LINK_TAG];
    if (sess.t != i || sess.peer != src || flags != want || seq <= sess.rx_seq) {
        return;
    }
    frame_tag(tag, f, UMCUB_LINK_HDR + plen, KEY_MAC_H2D);
    if (_compare(tag, f + UMCUB_LINK_HDR + plen, UMCUB_LINK_TAG) != 0) {
        return;                                     /* forged or damaged */
    }
    (void)len;
    sess.rx_seq = seq;
    sess.last_ms = now;
    s->peer = src;
    wakeup = true;
    if (type == UMCUB_LT_CLOSE) {
        end_session();
        return;
    }
#if UMCUB_CFG_LINK_ENCRYPT
    if (plen) {
        ctr_crypt(plain_buf, p, plen, seq, KEY_ENC_H2D);
        p = plain_buf;
    }
#endif
    (void)umcub_mux_link_rx(t, p, plen);           /* busy: dropped, the host retries */
}
#endif

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

#if UMCUB_CFG_LINK_SECURE_ANY
    if (t->link == UMCUB_LINK_SECURE) {
        secure_rx(t, i, s, f, len);
        return;
    }
#endif
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
        break;                                      /* SECURE types on an ADDRESSED transport */
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
    if (i < 0) {
        return;
    }
#if UMCUB_CFG_LINK_SECURE_ANY
    if (t->link == UMCUB_LINK_SECURE && sess.t != i) {
        return;                                     /* session ended meanwhile: never in clear */
    }
#endif
    send_frame(t, &st[i], UMCUB_LT_DATA, st[i].peer, data, len);
}

void umcub_link_poll(void)
{
    uint32_t now = umcub_port_millis();
#if UMCUB_CFG_LINK_SECURE_ANY
    if (sess.t >= 0 && now - sess.last_ms > UMCUB_CFG_LINK_SESSION_MS) {
        end_session();                              /* idle: keys wiped */
    }
#endif
    for (unsigned i = 0; i < umcub_transport_count && i < MAX_T; i++) {
        struct link_state *s = &st[i];
        if (s->announce_pending && (int32_t)(now - s->announce_at) >= 0) {
            s->announce_pending = false;
            announce(umcub_transports[i], s, s->announce_to);
        }
    }
}

bool umcub_link_confidential(const umcub_transport_t *t)
{
#if UMCUB_CFG_LINK_SECURE_ANY && UMCUB_CFG_LINK_ENCRYPT
    int i = index_of(t);
    return i >= 0 && t->link == UMCUB_LINK_SECURE && sess.t == i;
#else
    (void)t;
    return false;
#endif
}

bool umcub_link_take_wakeup(void)
{
    bool w = wakeup;
    wakeup = false;
    return w;
}
