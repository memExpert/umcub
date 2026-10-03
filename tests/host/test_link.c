/*
 * umcub host test: umcub link in ADDRESSED mode (transport/link.c + mux) on a
 * stream transport (frames as base64 lines) and a packet transport, next to
 * a plain stream transport.
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

extern const struct boot_uart_funcs *boot_uf;
const struct boot_uart_funcs *umcub_mux_funcs_for_test(void);
extern uint16_t fake_node_addr;

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
    uint8_t in[4096];
    size_t in_len, in_pos;
    char out[16384];
    size_t out_len;
} stream_t;
static stream_t s_link, s_plain;

static size_t s_read(stream_t *s, uint8_t *b, size_t m)
{
    size_t n = 0;
    while (n < m && s->in_pos < s->in_len) {
        b[n++] = s->in[s->in_pos++];
    }
    return n;
}
static void s_write(stream_t *s, const uint8_t *b, size_t l)
{
    if (s->out_len + l < sizeof(s->out)) {
        memcpy(&s->out[s->out_len], b, l);
        s->out_len += l;
    }
}
static size_t link_read(uint8_t *b, size_t m) { return s_read(&s_link, b, m); }
static void link_write(const uint8_t *b, size_t l) { s_write(&s_link, b, l); }
static size_t plain_read(uint8_t *b, size_t m) { return s_read(&s_plain, b, m); }
static void plain_write(const uint8_t *b, size_t l) { s_write(&s_plain, b, l); }

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

static const umcub_transport_t t_link = {
    .id = UMCUB_TRANSPORT_UART, .name = "rs485", .init = nop_init, .deinit = nop_deinit,
    .read = link_read, .write = link_write, .link = UMCUB_LINK_ADDRESSED,
};
static const umcub_transport_t t_pkt = {
    .id = UMCUB_TRANSPORT_CAN, .name = "can", .init = nop_init, .deinit = nop_deinit,
    .send_packet = pkt_send, .link = UMCUB_LINK_ADDRESSED,
};
static const umcub_transport_t t_plain = {
    .id = UMCUB_TRANSPORT_USB_CDC, .name = "cdc", .init = nop_init, .deinit = nop_deinit,
    .read = plain_read, .write = plain_write,
};
const umcub_transport_t *const umcub_transports[] = { &t_link, &t_pkt, &t_plain, 0 };
const unsigned umcub_transport_count = 3;
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

/* ---- frames -------------------------------------------------------------- */
static uint32_t host_seq;

static size_t frame(uint8_t *f, uint8_t type, uint8_t flags, uint16_t dst, const void *p, size_t len)
{
    f[0] = UMCUB_LINK_MAGIC;
    f[1] = UMCUB_LINK_VERSION;
    f[2] = type;
    f[3] = flags;
    f[4] = (uint8_t)dst;
    f[5] = (uint8_t)(dst >> 8);
    f[6] = 0xFE;                    /* host address 0xFFFE */
    f[7] = 0xFF;
    host_seq++;
    memcpy(f + 8, &host_seq, 4);
    f[12] = (uint8_t)len;
    f[13] = (uint8_t)(len >> 8);
    if (len) {
        memcpy(f + 14, p, len);
    }
    return 14 + len;
}

static void stream_send(uint8_t type, uint8_t flags, uint16_t dst, const void *p, size_t len)
{
    uint8_t f[1200];
    size_t n = frame(f, type, flags, dst, p, len);
    stream_t *s = &s_link;
    s->in[s->in_len++] = UMCUB_LINK_LINE_START1;
    s->in[s->in_len++] = UMCUB_LINK_LINE_START2;
    s->in_len += (size_t)base64_encode(f, (int)n, (char *)&s->in[s->in_len], 1);
    s->in[s->in_len++] = '\n';
}

static void run(int n)
{
    char tmp[1500];
    int nl;
    for (int i = 0; i < n; i++) {
        boot_uf->read(tmp, sizeof(tmp), &nl);
    }
}

/* Decode the k-th link line written to the stream; returns frame length. */
static size_t out_frame(int k, uint8_t *f)
{
    const char *p = s_link.out, *end = s_link.out + s_link.out_len;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        if (!nl) {
            break;
        }
        if (p[0] == UMCUB_LINK_LINE_START1 && p[1] == UMCUB_LINK_LINE_START2 && k-- == 0) {
            char line[2048];
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

static size_t smp_echo(uint8_t *p, uint8_t seq)
{
    static const uint8_t cbor[] = { 0xA1, 0x61, 'd', 0x62, 'h', 'i' };
    const uint8_t hdr[8] = { 2, 0, 0, sizeof(cbor), 0, 0, seq, 0 };
    memcpy(p, hdr, 8);
    memcpy(p + 8, cbor, sizeof(cbor));
    return 8 + sizeof(cbor);
}

static void reset_io(void)
{
    s_link.in_len = s_link.in_pos = s_link.out_len = 0;
    s_plain.in_len = s_plain.in_pos = s_plain.out_len = 0;
    pkt_out_len = 0;
}

/* ---- tests --------------------------------------------------------------- */

static void test_ignores_non_frames(void)
{
    printf("[link] plain NLIP and text on a link transport are ignored\n");
    reset_io();
    const char junk[] = "\x06\x09" "AAAAAA==\n" "i\n" "\x05\x0B" "!!!notbase64\n";
    memcpy(s_link.in, junk, sizeof(junk) - 1);
    s_link.in_len = sizeof(junk) - 1;
    run(20);
    CHECK(s_link.out_len == 0);
}

static void test_hello_and_addressing(void)
{
    printf("[link] HELLO to node 5 -> ANNOUNCE; frames for node 6 ignored\n");
    reset_io();
    stream_send(UMCUB_LT_HELLO, 0, 6, NULL, 0);
    run(10);
    CHECK(s_link.out_len == 0);
    CHECK(!umcub_link_take_wakeup());
    stream_send(UMCUB_LT_HELLO, 0, 5, NULL, 0);
    run(10);
    uint8_t f[2048];
    size_t n = out_frame(0, f);
    CHECK(n == 14 + 24 && f[2] == UMCUB_LT_ANNOUNCE);
    CHECK(f[4] == 0xFE && f[5] == 0xFF && f[6] == 5 && f[7] == 0);          /* to host, from 5 */
    uint32_t bt = (uint32_t)f[14 + 14] | (uint32_t)f[14 + 15] << 8 | (uint32_t)f[14 + 16] << 16 |
                  (uint32_t)f[14 + 17] << 24;
    CHECK(bt == UMCUB_CFG_BOARD_TYPE && f[14 + 12] == 5 && f[14 + 20] == UMCUB_LINK_ADDRESSED);
    CHECK(umcub_link_take_wakeup());
}

static void test_data_smp_and_text(void)
{
    printf("[link] DATA: SMP echo and text command over a stream and a packet transport\n");
    reset_io();
    uint8_t p[64];
    size_t n = smp_echo(p, 11);
    stream_send(UMCUB_LT_DATA, 0, 5, p, n);
    run(10);
    uint8_t f[2048];
    size_t fl = out_frame(0, f);
    CHECK(fl > 14 + 8 && f[2] == UMCUB_LT_DATA && f[14] == 3 && f[14 + 6] == 11 &&
          memmem(f + 14 + 8, fl - 22, "hi", 2) != NULL);

    reset_io();
    stream_send(UMCUB_LT_DATA, 0, 5, "i", 1);
    run(10);
    fl = out_frame(0, f);
    CHECK(fl > 14 && f[2] == UMCUB_LT_DATA && memmem(f + 14, fl - 14, "board type", 10) != NULL);

    reset_io();
    uint8_t fr[128];
    size_t frl = frame(fr, UMCUB_LT_DATA, 0, 5, p, smp_echo(p, 12));
    CHECK(umcub_smp_packet_rx(&t_pkt, fr, frl));
    run(10);
    CHECK(pkt_out_len > 22 && pkt_out[0] == UMCUB_LINK_MAGIC && pkt_out[2] == UMCUB_LT_DATA &&
          pkt_out[14 + 6] == 12);

    reset_io();                     /* a raw SMP packet (no frame) on the link packet transport */
    size_t raw = smp_echo(p, 13);
    CHECK(!umcub_smp_packet_rx(&t_pkt, p, raw));    /* not a frame: not accepted (peer unchanged) */
    run(10);
    CHECK(pkt_out_len == 0);
}

static void test_plain_transport_unchanged(void)
{
    printf("[link] the plain transport next to it still answers text\n");
    reset_io();
    memcpy(s_plain.in, "i\n", 2);
    s_plain.in_len = 2;
    run(10);
    CHECK(memmem(s_plain.out, s_plain.out_len, "board type", 10) != NULL);
}

static void test_discover(void)
{
    printf("[link] DISCOVER: ANNOUNCE in a random slot, silent when already found\n");
    reset_io();
    uint8_t d[3 + 12] = { 4, 5, 0 };    /* 4 slots of 5 ms */
    stream_send(UMCUB_LT_DISCOVER, 0, UMCUB_LINK_BROADCAST, d, 3);
    run(200);
    uint8_t f[2048];
    CHECK(out_frame(0, f) == 14 + 24 && f[2] == UMCUB_LT_ANNOUNCE);
    reset_io();
    umcub_port_uid(d + 3);
    stream_send(UMCUB_LT_DISCOVER, 0, UMCUB_LINK_BROADCAST, d, sizeof(d));
    run(200);
    CHECK(s_link.out_len == 0);
}

static void test_unassigned_uid_select(void)
{
    printf("[link] unassigned node: reachable only after HELLO with its UID\n");
    fake_node_addr = 0;
    reset_io();
    stream_send(UMCUB_LT_DATA, 0, 0, "i", 1);
    run(10);
    CHECK(s_link.out_len == 0);
    uint8_t uid[12];
    umcub_port_uid(uid);
    stream_send(UMCUB_LT_HELLO, UMCUB_LF_UID, 0, uid, sizeof(uid));
    stream_send(UMCUB_LT_DATA, 0, 0, "i", 1);
    run(20);
    uint8_t f[2048];
    CHECK(out_frame(0, f) && f[2] == UMCUB_LT_ANNOUNCE);
    CHECK(out_frame(1, f) && f[2] == UMCUB_LT_DATA);
    reset_io();
    uid[0] ^= 1;                    /* host selects another device */
    stream_send(UMCUB_LT_HELLO, UMCUB_LF_UID, 0, uid, sizeof(uid));
    stream_send(UMCUB_LT_DATA, 0, 0, "i", 1);
    run(20);
    CHECK(s_link.out_len == 0);
    fake_node_addr = 5;
}

static void test_garbage(void)
{
    printf("[link] 20000 random / truncated / oversized frames: no answer, no crash\n");
    srand(1);
    for (int i = 0; i < 20000; i++) {
        uint8_t f[96];
        size_t n = (size_t)(rand() % (int)sizeof(f));
        for (size_t k = 0; k < n; k++) {
            f[k] = (uint8_t)rand();
        }
        if (n > 1 && (i & 1)) {
            f[0] = UMCUB_LINK_MAGIC;
            f[1] = UMCUB_LINK_VERSION;
            if (n > 7) {
                f[4] = 7;           /* other node: never answered */
                f[5] = 0;
            }
        }
        pkt_out_len = 0;
        (void)umcub_smp_packet_rx(&t_pkt, f, n);
        run(1);
        CHECK(pkt_out_len == 0 || (i & 1) == 0);
    }
    (void)umcub_link_take_wakeup();
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    boot_uf = umcub_mux_funcs_for_test();
    fake_node_addr = 5;
    test_ignores_non_frames();
    test_hello_and_addressing();
    test_data_smp_and_text();
    test_plain_transport_unchanged();
    test_discover();
    test_unassigned_uid_select();
    test_garbage();
    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
