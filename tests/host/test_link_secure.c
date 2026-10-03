/*
 * umcub host test: umcub link in SECURE mode with payload encryption
 * (transport/link.c + mux) on a stream and a packet transport. The test plays
 * the host with tinycrypt: challenge signature with the admin key, ECDH with
 * the device key, HKDF, per-frame MAC, AES-CTR.
 */
#define _GNU_SOURCE     /* memmem */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_port.h"
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "umcub_transport.h"
#include "umcub_link.h"
#include "umcub_handoff.h"
#include "umcub_cmd.h"
#include "boot_serial/boot_serial.h"
#include "base64/base64.h"
#include "tinycrypt/constants.h"
#include "tinycrypt/ecc.h"
#include "tinycrypt/ecc_dh.h"
#include "tinycrypt/ecc_dsa.h"
#include "tinycrypt/hmac.h"
#include "tinycrypt/sha256.h"
#include "tinycrypt/aes.h"
#include "tinycrypt/ctr_mode.h"

extern const struct boot_uart_funcs *boot_uf;
const struct boot_uart_funcs *umcub_mux_funcs_for_test(void);
extern uint16_t fake_node_addr;
extern const uint8_t umcub_link_host_admin_priv[32];
extern const uint8_t umcub_link_host_device_pub[64];

static int failures;
#define CHECK(c)                                                                 \
    do {                                                                         \
        if (!(c)) {                                                              \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                \
            failures++;                                                          \
        }                                                                        \
    } while (0)

/* ---- transports ---------------------------------------------------------- */
typedef struct {
    uint8_t in[8192];
    size_t in_len, in_pos;
    char out[16384];
    size_t out_len;
} stream_t;
static stream_t s_sec;

static size_t sec_read(uint8_t *b, size_t m)
{
    size_t n = 0;
    while (n < m && s_sec.in_pos < s_sec.in_len) {
        b[n++] = s_sec.in[s_sec.in_pos++];
    }
    return n;
}
static void sec_write(const uint8_t *b, size_t l)
{
    if (s_sec.out_len + l < sizeof(s_sec.out)) {
        memcpy(&s_sec.out[s_sec.out_len], b, l);
        s_sec.out_len += l;
    }
}

static uint8_t pkt_out[2048];
static size_t pkt_out_len;
static int pkt_send(const uint8_t *p, size_t l)
{
    memcpy(pkt_out, p, l);
    pkt_out_len = l;
    return 0;
}
static int nop_init(void) { return 0; }
static void nop_deinit(void) {}

static const umcub_transport_t t_sec = {
    .id = UMCUB_TRANSPORT_UART, .name = "rs485", .init = nop_init, .deinit = nop_deinit,
    .read = sec_read, .write = sec_write, .link = UMCUB_LINK_SECURE,
};
static const umcub_transport_t t_pkt = {
    .id = UMCUB_TRANSPORT_CAN, .name = "can", .init = nop_init, .deinit = nop_deinit,
    .send_packet = pkt_send, .link = UMCUB_LINK_SECURE,
};
const umcub_transport_t *const umcub_transports[] = { &t_sec, &t_pkt, 0 };
const unsigned umcub_transport_count = 2;
void umcub_transports_poll(void) {}
void umcub_transports_init(void) {}
void umcub_transports_deinit(void) {}

bool umcub_cmd_user(unsigned id, const char *text, umcub_cmd_reply_t reply)
{
    (void)id;
    (void)text;
    (void)reply;
    return false;
}

/* ---- host side ----------------------------------------------------------- */
#define HOST        0xFFFEu
#define NODE        5u
#define HDR         UMCUB_LINK_HDR
#define TAG         UMCUB_LINK_TAG

static uint8_t keys[96];            /* mac h2d | mac d2h | enc h2d | enc d2h */
static uint32_t host_seq;
static uint16_t host_addr = HOST;
static uint32_t dev_seq;            /* last accepted device seq */

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
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void hmac(uint8_t out[32], const uint8_t *key, size_t klen, const void *a, size_t alen,
                 const void *b, size_t blen, const void *c, size_t clen)
{
    struct tc_hmac_state_struct h;
    tc_hmac_set_key(&h, key, (unsigned)klen);
    tc_hmac_init(&h);
    tc_hmac_update(&h, a, (unsigned)alen);
    if (blen) {
        tc_hmac_update(&h, b, (unsigned)blen);
    }
    if (clen) {
        tc_hmac_update(&h, c, (unsigned)clen);
    }
    tc_hmac_final(out, 32, &h);
}

static void ctr(uint8_t *buf, size_t len, uint32_t seq, const uint8_t *key)
{
    struct tc_aes_key_sched_struct ks;
    uint8_t c[16] = { 0 };
    uint32_t off = 0;
    put32(c, seq);
    tc_aes128_set_encrypt_key(&ks, key);
    tc_ctr_mode(buf, (unsigned)len, buf, (unsigned)len, c, &off, &ks);
}

/* Frame into f; session frames get ENC (DATA) and the tag. */
static size_t frame(uint8_t *f, uint8_t type, uint8_t flags, uint16_t dst, const void *p, size_t len,
                    bool session)
{
    f[0] = UMCUB_LINK_MAGIC;
    f[1] = UMCUB_LINK_VERSION;
    f[2] = type;
    f[3] = flags;
    put16(f + 4, dst);
    put16(f + 6, host_addr);
    put32(f + 8, ++host_seq);
    put16(f + 12, (uint16_t)len);
    if (len) {
        memcpy(f + HDR, p, len);
    }
    if (!session) {
        return HDR + len;
    }
    f[3] |= UMCUB_LF_MAC;
    if (type == UMCUB_LT_DATA && len) {
        f[3] |= UMCUB_LF_ENC;
        ctr(f + HDR, len, host_seq, &keys[64]);
    }
    uint8_t d[32];
    hmac(d, keys, 32, f, HDR + len, NULL, 0, NULL, 0);
    memcpy(f + HDR + len, d, TAG);
    return HDR + len + TAG;
}

static void stream_put(const uint8_t *f, size_t n)
{
    s_sec.in[s_sec.in_len++] = UMCUB_LINK_LINE_START1;
    s_sec.in[s_sec.in_len++] = UMCUB_LINK_LINE_START2;
    s_sec.in_len += (size_t)base64_encode(f, (int)n, (char *)&s_sec.in[s_sec.in_len], 1);
    s_sec.in[s_sec.in_len++] = '\n';
}

static bool on_pkt;                 /* host talks over t_pkt instead of t_sec */
static uint8_t last_frame[1500];
static size_t last_frame_len;

static void send(uint8_t type, uint8_t flags, uint16_t dst, const void *p, size_t len, bool session)
{
    last_frame_len = frame(last_frame, type, flags, dst, p, len, session);
    if (on_pkt) {
        CHECK(umcub_smp_packet_rx(&t_pkt, last_frame, last_frame_len));
    } else {
        stream_put(last_frame, last_frame_len);
    }
}

static void resend_raw(const uint8_t *f, size_t n)
{
    if (on_pkt) {
        CHECK(umcub_smp_packet_rx(&t_pkt, f, n));
    } else {
        stream_put(f, n);
    }
}

static void run(int n)
{
    char tmp[1500];
    int nl;
    for (int i = 0; i < n; i++) {
        boot_uf->read(tmp, sizeof(tmp), &nl);
    }
}

static void reset_io(void)
{
    s_sec.in_len = s_sec.in_pos = s_sec.out_len = 0;
    pkt_out_len = 0;
}

/* k-th frame the device sent on the current transport; returns its length. */
static size_t out_frame(int k, uint8_t *f)
{
    if (on_pkt) {
        if (k != 0 || !pkt_out_len) {
            return 0;
        }
        memcpy(f, pkt_out, pkt_out_len);
        return pkt_out_len;
    }
    const char *p = s_sec.out, *end = s_sec.out + s_sec.out_len;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        if (!nl) {
            break;
        }
        if (p[0] == UMCUB_LINK_LINE_START1 && p[1] == UMCUB_LINK_LINE_START2 && k-- == 0) {
            char line[4096];
            size_t l = (size_t)(nl - p - 2);
            memcpy(line, p + 2, l);
            line[l] = 0;
            int n = base64_decode(line, f);
            return n > 0 ? (size_t)n : 0;
        }
        p = nl + 1;
    }
    return 0;
}

static bool nothing_sent(void)
{
    return on_pkt ? pkt_out_len == 0 : s_sec.out_len == 0;
}

/* Device session frame: tag valid, seq increasing; decrypts DATA in place.
 * Returns the payload length or -1. */
static int open_dev(uint8_t *f, size_t n)
{
    if (n < HDR + TAG || !(f[3] & UMCUB_LF_MAC)) {
        return -1;
    }
    size_t len = (size_t)f[12] | (size_t)f[13] << 8;
    if (HDR + len + TAG != n) {
        return -1;
    }
    uint8_t d[32];
    hmac(d, &keys[32], 32, f, HDR + len, NULL, 0, NULL, 0);
    if (memcmp(d, f + HDR + len, TAG) != 0 || get32(f + 8) <= dev_seq) {
        return -1;
    }
    dev_seq = get32(f + 8);
    if (f[3] & UMCUB_LF_ENC) {
        ctr(f + HDR, len, get32(f + 8), &keys[80]);
    }
    return (int)len;
}

/* HELLO -> CHALLENGE; returns the challenge payload length (56) or 0. */
static size_t hello(uint16_t dst, uint8_t chal[56])
{
    reset_io();
    send(UMCUB_LT_HELLO, 0, dst, NULL, 0, false);
    run(10);
    uint8_t f[256];
    size_t n = out_frame(0, f);
    if (n != HDR + 56 || f[2] != UMCUB_LT_CHALLENGE) {
        return 0;
    }
    memcpy(chal, f + HDR, 56);
    return 56;
}

/* AUTH payload for `chal`, signed with `admin` (the right key or another). */
static void make_auth(uint8_t auth[160], const uint8_t chal[56], const uint8_t admin[32], uint8_t eph_priv[32])
{
    uECC_Curve c = uECC_secp256r1();
    CHECK(uECC_make_key(auth, eph_priv, c));
    for (int i = 0; i < 32; i++) {
        auth[64 + i] = (uint8_t)(0xA0 + i);         /* nonce_h */
    }
    uint8_t th[32], h2[2];
    struct tc_sha256_state_struct h;
    put16(h2, host_addr);
    tc_sha256_init(&h);
    tc_sha256_update(&h, (const uint8_t *)"umcub-link-v1", 13);
    tc_sha256_update(&h, h2, 2);
    tc_sha256_update(&h, chal, 56);
    tc_sha256_update(&h, auth, 96);
    tc_sha256_final(th, &h);
    CHECK(uECC_sign(admin, th, 32, auth + 96, c));
    /* Session keys exactly as the device derives them. */
    uint8_t shared[32], prk[32];
    CHECK(uECC_shared_secret(umcub_link_host_device_pub, eph_priv, shared, c));
    static const char info[] = "umcub-link-v1 keys";
    hmac(prk, th, 32, shared, 32, NULL, 0, NULL, 0);
    hmac(&keys[0], prk, 32, info, sizeof(info) - 1, "\x01", 1, NULL, 0);
    hmac(&keys[32], prk, 32, &keys[0], 32, info, sizeof(info) - 1, "\x02", 1);
    hmac(&keys[64], prk, 32, &keys[32], 32, info, sizeof(info) - 1, "\x03", 1);
}

/* Full handshake with node `dst`; true if AUTH_OK verified. */
static bool handshake(uint16_t dst)
{
    uint8_t chal[56], auth[160], eph[32];
    if (!hello(dst, chal)) {
        return false;
    }
    make_auth(auth, chal, umcub_link_host_admin_priv, eph);
    reset_io();
    dev_seq = 0;
    send(UMCUB_LT_AUTH, 0, dst, auth, sizeof(auth), false);
    run(10);
    uint8_t f[256];
    size_t n = out_frame(0, f);
    return n == HDR + TAG && f[2] == UMCUB_LT_AUTH_OK && open_dev(f, n) == 0;
}

/* Send text command `cmd` in the session; true if the decrypted answer has `want`. */
static bool session_cmd(const char *cmd, const char *want)
{
    reset_io();
    send(UMCUB_LT_DATA, 0, NODE, cmd, strlen(cmd), true);
    run(10);
    uint8_t f[2048];
    size_t n = out_frame(0, f);
    if (!n || f[2] != UMCUB_LT_DATA || !(f[3] & UMCUB_LF_ENC)) {
        return false;
    }
    int l = open_dev(f, n);
    return l > 0 && memmem(f + HDR, (size_t)l, want, strlen(want)) != NULL;
}

/* ---- tests --------------------------------------------------------------- */

static void test_closed_without_auth(void)
{
    printf("[secure] DATA and text without a session: no answer\n");
    reset_io();
    send(UMCUB_LT_DATA, 0, NODE, "i", 1, false);
    run(10);
    CHECK(nothing_sent());
    memset(keys, 0x55, sizeof(keys));       /* MAC with keys the device never agreed on */
    reset_io();
    send(UMCUB_LT_DATA, 0, NODE, "i", 1, true);
    run(10);
    CHECK(nothing_sent());
    reset_io();
    const char text[] = "i\n";
    memcpy(s_sec.in, text, 2);
    s_sec.in_len = 2;
    run(10);
    CHECK(nothing_sent());
}

static void test_challenge(void)
{
    printf("[secure] HELLO -> CHALLENGE: identity (SECURE | ENC), fresh nonce every time\n");
    uint8_t c1[56], c2[56];
    CHECK(hello(NODE, c1) == 56);
    CHECK(hello(NODE, c2) == 56);
    CHECK(c1[12] == NODE && c1[20] == (UMCUB_LINK_SECURE | UMCUB_LINK_ANNOUNCE_ENC));
    CHECK(memcmp(c1, c2, 24) == 0 && memcmp(c1 + 24, c2 + 24, 32) != 0);
    CHECK(!umcub_link_take_wakeup());       /* no stay in the bootloader for strangers */
}

static void test_wrong_admin_key(void)
{
    printf("[secure] AUTH signed with another key: rejected, challenge used up\n");
    uint8_t chal[56], auth[160], eph[32], other[32];
    for (int i = 0; i < 32; i++) {
        other[i] = (uint8_t)(i + 1);
    }
    CHECK(hello(NODE, chal));
    make_auth(auth, chal, other, eph);
    reset_io();
    send(UMCUB_LT_AUTH, 0, NODE, auth, sizeof(auth), false);
    run(10);
    CHECK(nothing_sent());
    make_auth(auth, chal, umcub_link_host_admin_priv, eph);    /* right key, same challenge */
    reset_io();
    send(UMCUB_LT_AUTH, 0, NODE, auth, sizeof(auth), false);
    run(10);
    CHECK(nothing_sent());
}

static void test_session(void)
{
    printf("[secure] handshake, encrypted text and SMP, AUTH_OK authenticates the device\n");
    CHECK(handshake(NODE));
    CHECK(umcub_link_take_wakeup());
    CHECK(session_cmd("i", "board type"));

    static const uint8_t cbor[] = { 0xA1, 0x61, 'd', 0x62, 'h', 'i' };
    uint8_t smp[8 + sizeof(cbor)] = { 2, 0, 0, sizeof(cbor), 0, 0, 21, 0 };
    memcpy(smp + 8, cbor, sizeof(cbor));
    reset_io();
    send(UMCUB_LT_DATA, 0, NODE, smp, sizeof(smp), true);
    run(10);
    uint8_t f[2048];
    size_t n = out_frame(0, f);
    int l = open_dev(f, n);
    CHECK(l > 8 && f[HDR] == 3 && f[HDR + 6] == 21 && memmem(f + HDR + 8, (size_t)l - 8, "hi", 2));
    /* On the wire the answer is not readable. */
    CHECK(memmem(s_sec.out, s_sec.out_len, "aGk", 3) == NULL);
}

static void test_replay_tamper_order(void)
{
    printf("[secure] replayed, tampered, reordered and unencrypted frames are dropped\n");
    CHECK(handshake(NODE));
    reset_io();
    send(UMCUB_LT_DATA, 0, NODE, "i", 1, true);
    uint8_t keep[64];
    size_t keep_len = last_frame_len;
    memcpy(keep, last_frame, keep_len);
    run(10);
    CHECK(!nothing_sent());

    reset_io();                                     /* replay */
    resend_raw(keep, keep_len);
    run(10);
    CHECK(nothing_sent());

    for (size_t pos = 0; pos < keep_len; pos++) {   /* every single bit flip of a fresh frame */
        uint8_t f[64];
        host_seq++;
        reset_io();
        size_t n = frame(f, UMCUB_LT_DATA, 0, NODE, "i", 1, true);
        f[pos] ^= 0x04;
        resend_raw(f, n);
        run(4);
        uint8_t a[256];
        if (pos == 2) {                             /* DATA ^ 4 = HELLO: a new challenge, nothing else */
            CHECK(out_frame(0, a) == HDR + 56 && a[2] == UMCUB_LT_CHALLENGE && !out_frame(1, a));
        } else {
            CHECK(nothing_sent());
        }
    }

    reset_io();                                     /* older seq than the last accepted one */
    uint32_t s = host_seq;
    host_seq = 2;
    send(UMCUB_LT_DATA, 0, NODE, "i", 1, true);
    host_seq = s;
    run(10);
    CHECK(nothing_sent());

    reset_io();                                     /* MAC fine, but not encrypted */
    uint8_t f[64];
    size_t n = frame(f, UMCUB_LT_DATA, 0, NODE, "i", 1, false);
    f[3] = UMCUB_LF_MAC;
    uint8_t d[32];
    hmac(d, keys, 32, f, n, NULL, 0, NULL, 0);
    memcpy(f + n, d, TAG);
    resend_raw(f, n + TAG);
    run(10);
    CHECK(nothing_sent());

    reset_io();                                     /* another host address, same keys */
    host_addr = 0x1234;
    send(UMCUB_LT_DATA, 0, NODE, "i", 1, true);
    host_addr = HOST;
    run(10);
    CHECK(nothing_sent());

    CHECK(session_cmd("i", "board type"));          /* the session survived all of it */
}

static void test_readback_in_session(void)
{
    printf("[secure] encrypted images: readback answers inside the encrypted session\n");
    CHECK(handshake(NODE));
    CHECK(session_cmd("read 0 0 0 8", "00000000 0000000000000000"));
}

static void test_failed_auth_keeps_session(void)
{
    printf("[secure] a failed AUTH from someone else leaves the session open\n");
    CHECK(handshake(NODE));
    uint8_t saved[96];
    memcpy(saved, keys, sizeof(keys));
    uint8_t chal[56], auth[160], eph[32], other[32];
    memset(other, 7, sizeof(other));
    host_addr = 0x2222;
    CHECK(hello(NODE, chal));
    make_auth(auth, chal, other, eph);
    reset_io();
    send(UMCUB_LT_AUTH, 0, NODE, auth, sizeof(auth), false);
    run(10);
    CHECK(nothing_sent());
    host_addr = HOST;
    memcpy(keys, saved, sizeof(keys));
    CHECK(session_cmd("i", "board type"));
}

static void test_auth_replay(void)
{
    printf("[secure] a recorded AUTH does not open a session for a new challenge\n");
    uint8_t chal[56], auth[160], eph[32];
    CHECK(hello(NODE, chal));
    make_auth(auth, chal, umcub_link_host_admin_priv, eph);
    reset_io();
    send(UMCUB_LT_AUTH, 0, NODE, auth, sizeof(auth), false);
    run(10);
    CHECK(!nothing_sent());
    CHECK(hello(NODE, chal));
    reset_io();
    send(UMCUB_LT_AUTH, 0, NODE, auth, sizeof(auth), false);
    run(10);
    CHECK(nothing_sent());
}

static void test_close_and_timeout(void)
{
    printf("[secure] CLOSE and the idle timeout end the session\n");
    CHECK(handshake(NODE));
    reset_io();
    send(UMCUB_LT_CLOSE, 0, NODE, NULL, 0, true);
    run(10);
    CHECK(nothing_sent());
    reset_io();
    send(UMCUB_LT_DATA, 0, NODE, "i", 1, true);
    run(10);
    CHECK(nothing_sent());

    CHECK(handshake(NODE));
    CHECK(session_cmd("i", "board type"));
    umcub_port_delay_ms(UMCUB_CFG_LINK_SESSION_MS + 1);
    reset_io();
    send(UMCUB_LT_DATA, 0, NODE, "i", 1, true);
    run(10);
    CHECK(nothing_sent());
}

static void test_packet_transport(void)
{
    printf("[secure] handshake and session on the packet transport; stream stays closed\n");
    on_pkt = true;
    CHECK(handshake(NODE));
    CHECK(session_cmd("i", "board type"));
    on_pkt = false;
    reset_io();
    send(UMCUB_LT_DATA, 0, NODE, "i", 1, true);     /* same keys on the other transport */
    run(10);
    CHECK(nothing_sent());
}

static void test_rdp_closed(void)
{
    printf("[secure] RDP level 0: HELLO answered with ANNOUNCE (closed), no challenge\n");
    fake_rdp_level = 0;
    reset_io();
    send(UMCUB_LT_HELLO, 0, NODE, NULL, 0, false);
    run(10);
    uint8_t f[256];
    size_t n = out_frame(0, f);
    CHECK(n == HDR + 24 && f[2] == UMCUB_LT_ANNOUNCE && (f[HDR + 20] & UMCUB_LINK_ANNOUNCE_CLOSED));
    fake_rdp_level = 1;
}

static void test_garbage(void)
{
    printf("[secure] 20000 random frames for this node: no answer, no crash\n");
    srand(2);
    for (int i = 0; i < 20000; i++) {
        uint8_t f[200];
        size_t n = (size_t)(rand() % (int)sizeof(f));
        for (size_t k = 0; k < n; k++) {
            f[k] = (uint8_t)rand();
        }
        if (n >= HDR) {
            f[0] = UMCUB_LINK_MAGIC;
            f[1] = UMCUB_LINK_VERSION;
            f[2] = (uint8_t)(UMCUB_LT_AUTH + (i % 4));      /* AUTH, AUTH_OK, DATA, CLOSE */
            f[4] = NODE;
            f[5] = 0;
            put16(f + 12, (uint16_t)(n - HDR - ((i & 1) ? TAG : 0)));
        }
        pkt_out_len = 0;
        (void)umcub_smp_packet_rx(&t_pkt, f, n);
        run(1);
        CHECK(pkt_out_len == 0);
    }
    (void)umcub_link_take_wakeup();
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    boot_uf = umcub_mux_funcs_for_test();
    fake_node_addr = NODE;
    test_closed_without_auth();
    test_challenge();
    test_wrong_admin_key();
    test_session();
    test_replay_tamper_order();
    test_readback_in_session();
    test_failed_auth_keeps_session();
    test_auth_replay();
    test_close_and_timeout();
    test_packet_transport();
    test_rdp_closed();
    test_garbage();
    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
