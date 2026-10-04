/*
 * umcub host tests: real MCUboot (boot_go, boot_serial, ECDSA/tinycrypt)
 * on emulated STM32H7 flash, driven through the umcub transports layer.
 */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_port.h"
#include "umcub_inspect.h"
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "umcub_port_can.h"
#include "umcub_port_eth.h"
#include "umcub_transport.h"
#include "umcub_log.h"
#include "umcub.h"
#include "bootutil/bootutil.h"
#include "bootutil/image.h"
#include "bootutil/fault_injection_hardening.h"
#include "boot_serial/boot_serial.h"
#include "base64/base64.h"
#include "crc/crc16.h"
#include "../../transport/can/isotp.h"
#include "../../transport/net/net.h"
#include "umcub_cmd.h"
#include "umcub_image_info.h"
#include "sysflash/sysflash.h"

extern const struct boot_uart_funcs *boot_uf;
const struct boot_uart_funcs *umcub_mux_funcs_for_test(void);
void boot_serial_input(char *buf, int len);

static int failures;
#define CHECK(c)                                                                 \
    do {                                                                         \
        if (!(c)) {                                                              \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                \
            failures++;                                                          \
        }                                                                        \
    } while (0)

/* ------------------------------------------------------------------------ */
/* helpers                                                                  */
/* ------------------------------------------------------------------------ */

static uint8_t *load(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*len);
    if (fread(b, 1, *len, f) != *len) {
        exit(2);
    }
    fclose(f);
    return b;
}

/* minimal CBOR encoder */
typedef struct {
    uint8_t buf[2048];
    size_t n;
} cbor_t;

static void cb_head(cbor_t *c, uint8_t major, uint64_t v)
{
    if (v < 24) {
        c->buf[c->n++] = (uint8_t)(major << 5 | v);
    } else if (v < 256) {
        c->buf[c->n++] = (uint8_t)(major << 5 | 24);
        c->buf[c->n++] = (uint8_t)v;
    } else if (v < 65536) {
        c->buf[c->n++] = (uint8_t)(major << 5 | 25);
        c->buf[c->n++] = (uint8_t)(v >> 8);
        c->buf[c->n++] = (uint8_t)v;
    } else {
        c->buf[c->n++] = (uint8_t)(major << 5 | 26);
        for (int s = 24; s >= 0; s -= 8) {
            c->buf[c->n++] = (uint8_t)(v >> s);
        }
    }
}
static void cb_map(cbor_t *c, unsigned n) { cb_head(c, 5, n); }
static void cb_uint(cbor_t *c, uint64_t v) { cb_head(c, 0, v); }
static void cb_text(cbor_t *c, const char *s)
{
    cb_head(c, 3, strlen(s));
    memcpy(&c->buf[c->n], s, strlen(s));
    c->n += strlen(s);
}
static void cb_bytes(cbor_t *c, const uint8_t *p, size_t len)
{
    cb_head(c, 2, len);
    memcpy(&c->buf[c->n], p, len);
    c->n += len;
}

static size_t smp_frame(uint8_t *out, uint8_t op, uint16_t group, uint8_t id, const cbor_t *body)
{
    static uint8_t seq;
    out[0] = op;
    out[1] = 0;
    out[2] = (uint8_t)(body->n >> 8);
    out[3] = (uint8_t)body->n;
    out[4] = (uint8_t)(group >> 8);
    out[5] = (uint8_t)group;
    out[6] = seq++;
    out[7] = id;
    memcpy(out + 8, body->buf, body->n);
    return 8 + body->n;
}

/* Find integer value of a top-level text key in a CBOR map (naive scan). */
static long cbor_find_uint(const uint8_t *p, size_t len, const char *key)
{
    size_t kl = strlen(key);
    for (size_t i = 0; i + 1 + kl < len; i++) {
        if (p[i] == (0x60 | kl) && memcmp(&p[i + 1], key, kl) == 0) {
            const uint8_t *v = &p[i + 1 + kl];
            if (v[0] < 24) return v[0];
            if (v[0] == 24) return v[1];
            if (v[0] == 25) return v[1] << 8 | v[2];
            if (v[0] == 26) return (long)((uint32_t)v[1] << 24 | v[2] << 16 | v[3] << 8 | v[4]);
            if (v[0] >= 0x20 && v[0] < 0x38) return -1 - (v[0] - 0x20);
            return -1000;
        }
    }
    return -2000;
}

static bool contains_bytes(const uint8_t *p, size_t len, const uint8_t *s, size_t sl)
{
    for (size_t i = 0; i + sl <= len; i++) {
        if (memcmp(p + i, s, sl) == 0) {
            return true;
        }
    }
    return false;
}

static bool contains(const uint8_t *p, size_t len, const char *s)
{
    size_t sl = strlen(s);
    for (size_t i = 0; i + sl <= len; i++) {
        if (memcmp(p + i, s, sl) == 0) {
            return true;
        }
    }
    return false;
}

static bool boot_ok(char *version)
{
    struct boot_rsp rsp;
    FIH_DECLARE(rc, FIH_FAILURE);
    FIH_CALL(boot_go, rc, &rsp);
    if (FIH_NOT_EQ(rc, FIH_SUCCESS)) {
        strcpy(version, "fail");
        return false;
    }
    sprintf(version, "%u.%u.%u", rsp.br_hdr->ih_ver.iv_major, rsp.br_hdr->ih_ver.iv_minor,
            rsp.br_hdr->ih_ver.iv_revision);
    return rsp.br_image_off == UMCUB_CFG_IMG0_PRIMARY_ADDR;
}

/* ------------------------------------------------------------------------ */
/* fake transports                                                          */
/* ------------------------------------------------------------------------ */

static uint8_t pkt_rsp[2048];
static size_t pkt_rsp_len;

static int nop_init(void) { return 0; }
static void nop_deinit(void) {}

static int pkt_send(const uint8_t *p, size_t len)
{
    memcpy(pkt_rsp, p, len);
    pkt_rsp_len = len;
    return 0;
}

static const umcub_transport_t t_pkt = {
    .id = UMCUB_TRANSPORT_ETH, .name = "fake-pkt", .init = nop_init, .deinit = nop_deinit,
    .send_packet = pkt_send,
};

/* Two stream transports with scripted input and captured output. */
typedef struct {
    const uint8_t *in;
    size_t in_len, in_pos;
    size_t gate;            /* bytes available so far (simulates slow arrival) */
    char out[8192];
    size_t out_len;
} stream_t;
static stream_t streams[2];
static jmp_buf stream_done;
static int stream_polls;

static size_t s_read(stream_t *s, uint8_t *buf, size_t max)
{
    size_t n = 0;
    while (n < max && s->in_pos < s->in_len && s->in_pos < s->gate) {
        buf[n++] = s->in[s->in_pos++];
    }
    return n;
}
static size_t s0_read(uint8_t *b, size_t m) { return s_read(&streams[0], b, m); }
static size_t s1_read(uint8_t *b, size_t m) { return s_read(&streams[1], b, m); }
static void s_write(stream_t *s, const uint8_t *b, size_t l)
{
    memcpy(&s->out[s->out_len], b, l);
    s->out_len += l;
}
static void s0_write(const uint8_t *b, size_t l) { s_write(&streams[0], b, l); }
static void s1_write(const uint8_t *b, size_t l) { s_write(&streams[1], b, l); }

static void streams_poll(void)
{
    /* Release input gradually and interleaved: stream 0 one line ahead. */
    stream_polls++;
    for (int i = 0; i < 2; i++) {
        stream_t *s = &streams[i];
        if (s->gate < s->in_len) {
            s->gate += 40;
        }
    }
    if (stream_polls > 20000) {
        longjmp(stream_done, 1);
    }
}

static const umcub_transport_t t_s0 = {
    .id = UMCUB_TRANSPORT_UART, .name = "s0", .init = nop_init, .deinit = nop_deinit,
    .poll = streams_poll, .read = s0_read, .write = s0_write,
};
static const umcub_transport_t t_s1 = {
    .id = UMCUB_TRANSPORT_USB_CDC, .name = "s1", .init = nop_init, .deinit = nop_deinit,
    .read = s1_read, .write = s1_write,
};

const umcub_transport_t *const umcub_transports[] = { &t_s0, &t_s1, &t_pkt, 0 };
const unsigned umcub_transport_count = 3;
void umcub_transports_poll(void)
{
    for (unsigned i = 0; i < umcub_transport_count; i++) {
        if (umcub_transports[i]->poll) {
            umcub_transports[i]->poll();
        }
    }
}
void umcub_transports_init(void) {}
void umcub_transports_deinit(void) {}

/* NLIP-encode a raw SMP packet the way mcumgr does (lines of <= 127 bytes). */
static size_t nlip_encode(const uint8_t *pkt, size_t len, char *out)
{
    uint8_t raw[2048];
    raw[0] = (uint8_t)((len + 2) >> 8);
    raw[1] = (uint8_t)(len + 2);
    memcpy(raw + 2, pkt, len);
    uint16_t crc = crc16_ccitt(0, pkt, (int)len);
    raw[2 + len] = (uint8_t)(crc >> 8);
    raw[3 + len] = (uint8_t)crc;
    char b64[4096];
    int bl = base64_encode(raw, (int)len + 4, b64, 1);
    size_t o = 0;
    for (int off = 0; off < bl; off += 124) {
        out[o++] = off == 0 ? 6 : 4;
        out[o++] = off == 0 ? 9 : 20;
        int n = bl - off < 124 ? bl - off : 124;
        memcpy(&out[o], &b64[off], (size_t)n);
        o += (size_t)n;
        out[o++] = '\n';
    }
    return o;
}

/* Decode the first NLIP packet in a captured stream. */
static size_t nlip_decode(const char *in, size_t in_len, uint8_t *pkt)
{
    uint8_t raw[4096];
    size_t raw_len = 0;
    char line[256];
    size_t i = 0;
    while (i < in_len) {
        size_t l = 0;
        while (i < in_len && in[i] != '\n' && l < sizeof(line) - 1) {
            line[l++] = in[i++];
        }
        i++;
        line[l] = 0;
        if (l > 2 && ((line[0] == 6 && line[1] == 9) || (line[0] == 4 && line[1] == 20))) {
            raw_len += (size_t)base64_decode(&line[2], &raw[raw_len]);
            if (raw_len >= 2 && raw_len >= (size_t)(raw[0] << 8 | raw[1]) + 2) {
                size_t total = (size_t)(raw[0] << 8 | raw[1]);
                if (crc16_ccitt(0, raw + 2, (int)total) != 0) {
                    return 0;
                }
                memcpy(pkt, raw + 2, total - 2);
                return total - 2;
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* tests                                                                    */
/* ------------------------------------------------------------------------ */

static void smp_packet_request(const uint8_t *pkt, size_t len)
{
    pkt_rsp_len = 0;
    CHECK(umcub_smp_packet_rx(&t_pkt, pkt, len));
    int nl;
    char tmp[1100];
    boot_uf->read(tmp, sizeof(tmp), &nl);    /* processes the pending packet */
}

static void upload_packets(const uint8_t *img, size_t len, int image)
{
    size_t off = 0;
    while (off < len) {
        size_t n = len - off > 512 ? 512 : len - off;
        cbor_t c = { .n = 0 };
        cb_map(&c, off == 0 ? 4 : 3);
        cb_text(&c, "image");
        cb_uint(&c, (uint64_t)image);
        if (off == 0) {
            cb_text(&c, "len");
            cb_uint(&c, len);
        }
        cb_text(&c, "off");
        cb_uint(&c, off);
        cb_text(&c, "data");
        cb_bytes(&c, img + off, n);
        uint8_t pkt[1100];
        size_t pl = smp_frame(pkt, 2, 1, 1, &c);
        smp_packet_request(pkt, pl);
        CHECK(pkt_rsp_len > 8);
        CHECK(pkt_rsp[0] == 3);     /* write response */
        long rc = cbor_find_uint(pkt_rsp + 8, pkt_rsp_len - 8, "rc");
        long noff = cbor_find_uint(pkt_rsp + 8, pkt_rsp_len - 8, "off");
        if (rc > 0 || noff != (long)(off + n)) {
            printf("  upload rsp at %zu: rc %ld off %ld\n", off, rc, noff);
            failures++;
            return;
        }
        off += n;
    }
}

/* What an uploaded image looks like in the primary slot: the file itself, or
 * for an encrypted file its header and TLVs around the plain payload (the
 * bootloader decrypts it in place after the upload). */
static const uint8_t *in_flash(const uint8_t *img, size_t len, const uint8_t *plain)
{
    if (!plain) {
        return img;
    }
    uint8_t *x = malloc(len);
    uint32_t hdr = (uint32_t)img[8] | (uint32_t)img[9] << 8;
    uint32_t size = (uint32_t)img[12] | (uint32_t)img[13] << 8 | (uint32_t)img[14] << 16 |
                    (uint32_t)img[15] << 24;
    memcpy(x, img, len);
    memcpy(x + hdr, plain + hdr, size);
    return x;
}

static void test_packet_upload_and_boot(const uint8_t *v1, size_t v1_len, const uint8_t *v1_flash)
{
    printf("[packet transport] SMP echo, upload to primary, boot_go\n");
    fake_flash_reset();
    boot_uf = umcub_mux_funcs_for_test();

    cbor_t c = { .n = 0 };
    cb_map(&c, 1);
    cb_text(&c, "d");
    cb_text(&c, "hello umcub");
    uint8_t pkt[256];
    size_t pl = smp_frame(pkt, 2, 0, 0, &c);
    smp_packet_request(pkt, pl);
    CHECK(pkt_rsp_len > 8 && contains(pkt_rsp, pkt_rsp_len, "hello umcub"));
    CHECK(umcub_recovery_last_transport() == UMCUB_TRANSPORT_ETH);

    char ver[16];
    CHECK(!boot_ok(ver));                   /* empty flash: nothing to boot */
    upload_packets(v1, v1_len, 0);
    CHECK(memcmp(fake_flash + (UMCUB_CFG_IMG0_PRIMARY_ADDR - 0x08000000), v1_flash, v1_len) == 0);
#if UMCUB_CFG_ENCRYPT_IMAGES
    CHECK(memcmp(v1, v1_flash, v1_len) != 0);   /* it really was encrypted on the wire */
    printf("  encrypted upload decrypted in place\n");
    /* The header keeps IMAGE_F_ENCRYPTED (it is signed): in the primary slot
     * the image must still be treated as plain (MUST_DECRYPT is false). */
    struct image_header ph;
    memcpy(&ph, v1_flash, sizeof(ph));
    CHECK(IS_ENCRYPTED(&ph));
    CHECK(umcub_inspect_verify(0, 0) == 0);
    printf("  decrypted primary (header still flagged encrypted): verify ok\n");
#endif
    CHECK(boot_ok(ver) && strcmp(ver, "1.0.0") == 0);
    printf("  booted %s\n", ver);

    /* corrupt one byte: the signature check must refuse it */
    fake_flash[UMCUB_CFG_IMG0_PRIMARY_ADDR - 0x08000000 + 0x500] ^= 1;
    CHECK(!boot_ok(ver));
    fake_flash[UMCUB_CFG_IMG0_PRIMARY_ADDR - 0x08000000 + 0x500] ^= 1;
    CHECK(boot_ok(ver));
}

static void write_secondary(const uint8_t *img, size_t len)
{
    umcub_slot_writer_t w;
    CHECK(umcub_slot_begin(&w, 0, UMCUB_SLOT_DEFAULT, (uint32_t)len) == 0);
    CHECK(w.slot == 1);
    /* odd chunk sizes to exercise the alignment buffer */
    for (size_t off = 0; off < len;) {
        size_t n = len - off > 777 ? 777 : len - off;
        CHECK(umcub_slot_write(&w, img + off, n) == 0);
        off += n;
    }
    CHECK(umcub_slot_finish(&w, true, false) == 0);
}

#if UMCUB_CFG_ENCRYPT_IMAGES
static bool flash_read_hdr_secondary(struct image_header *h)
{
    const struct flash_area *fa;
    bool ok = flash_area_open(FLASH_AREA_IMAGE_SECONDARY(0), &fa) == 0 &&
              flash_area_read(fa, 0, h, sizeof(*h)) == 0 && h->ih_magic == IMAGE_MAGIC;
    flash_area_close(fa);
    return ok;
}
#endif

static void test_swap_revert_confirm(const uint8_t *v2, size_t v2_len)
{
    char ver[16];
    printf("[slot writer + swap-scratch] test boot, revert, confirm\n");
    write_secondary(v2, v2_len);
    CHECK(umcub_is_confirmed(0) == 1);      /* v1 was installed permanently */
    CHECK(boot_ok(ver) && strcmp(ver, "1.1.0") == 0);
    printf("  after test upgrade: %s\n", ver);
#if UMCUB_CFG_ENCRYPT_IMAGES
    /* secondary: the old image, encrypted again by the swap - verified while decrypting */
    struct image_header sh;
    CHECK(flash_read_hdr_secondary(&sh) && IS_ENCRYPTED(&sh));
    CHECK(umcub_inspect_verify(0, 1) == 0);
    printf("  old image re-encrypted in the secondary slot, verify ok\n");
#endif
    CHECK(umcub_is_confirmed(0) == 0);
    umcub_slot_writer_t busy;   /* secondary holds the revert copy now */
    CHECK(umcub_slot_begin(&busy, 0, UMCUB_SLOT_DEFAULT, (uint32_t)v2_len) == UMCUB_EBUSY);
    CHECK(boot_ok(ver) && strcmp(ver, "1.0.0") == 0);
    printf("  after reset without confirm (revert): %s\n", ver);

    write_secondary(v2, v2_len);
    CHECK(boot_ok(ver) && strcmp(ver, "1.1.0") == 0);
    CHECK(umcub_confirm_image(0) == 0);
    CHECK(umcub_is_confirmed(0) == 1);
    CHECK(boot_ok(ver) && strcmp(ver, "1.1.0") == 0);
    CHECK(boot_ok(ver) && strcmp(ver, "1.1.0") == 0);
    printf("  after confirm + 2 resets: %s\n", ver);
    umcub_version_t v;
    CHECK(umcub_image_version(0, 0, &v) == 0 && v.minor == 1);
}

/* An image signed with the right key but for another board type is never
 * installed (MCUboot image check hook, mcuboot_port/src/hooks.c). */
static void test_board_type(const uint8_t *foreign, size_t len)
{
    char ver[16];
    printf("[board type] image for board 0x%08x refused, 0x%08x keeps running\n",
           (unsigned)UMCUB_CFG_BOARD_TYPE + 1u, (unsigned)UMCUB_CFG_BOARD_TYPE);
    const struct flash_area *fa;
    uint32_t type = 0;
    CHECK(flash_area_open(FLASH_AREA_IMAGE_PRIMARY(0), &fa) == 0);
    CHECK(umcub_image_board_type(fa, 0, &type) == 0 && type == UMCUB_CFG_BOARD_TYPE);
    flash_area_close(fa);
    write_secondary(foreign, len);
    CHECK(boot_ok(ver) && strcmp(ver, "1.1.0") == 0);
    struct image_header h;
    CHECK(flash_area_open(FLASH_AREA_IMAGE_SECONDARY(0), &fa) == 0);
    CHECK(flash_area_read(fa, 0, &h, sizeof(h)) == 0 && h.ih_magic != IMAGE_MAGIC);   /* erased by MCUboot */
    flash_area_close(fa);
}

static void test_streams_interleaved(const uint8_t *img, size_t len)
{
    printf("[stream transports] interleaved NLIP: upload on s0, echo on s1, list on s0\n");
    static char in0[65536], in1[1024];
    size_t n0 = 0, n1 = 0;

    /* s0: first chunk of an upload (spans many NLIP lines) then image list */
    cbor_t c = { .n = 0 };
    cb_map(&c, 4);
    cb_text(&c, "image");
    cb_uint(&c, 0);
    cb_text(&c, "len");
    cb_uint(&c, len);
    cb_text(&c, "off");
    cb_uint(&c, 0);
    cb_text(&c, "data");
    cb_bytes(&c, img, 512);
    uint8_t pkt[1100];
    size_t pl = smp_frame(pkt, 2, 1, 1, &c);
    n0 += nlip_encode(pkt, pl, in0 + n0);
    memcpy(in0 + n0, "some console noise\n", 19);
    n0 += 19;

    cbor_t e = { .n = 0 };
    cb_map(&e, 1);
    cb_text(&e, "d");
    cb_text(&e, "from s1");
    pl = smp_frame(pkt, 2, 0, 0, &e);
    n1 += nlip_encode(pkt, pl, in1 + n1);

    memset(streams, 0, sizeof(streams));
    streams[0].in = (uint8_t *)in0;
    streams[0].in_len = n0;
    streams[1].in = (uint8_t *)in1;
    streams[1].in_len = n1;
    stream_polls = 0;

    if (setjmp(stream_done) == 0) {
        boot_serial_start(umcub_mux_funcs_for_test());
    }
    uint8_t rsp[2048];
    size_t rl = nlip_decode(streams[0].out, streams[0].out_len, rsp);
    CHECK(rl > 8 && cbor_find_uint(rsp + 8, rl - 8, "off") == 512);
    rl = nlip_decode(streams[1].out, streams[1].out_len, rsp);
    CHECK(rl > 8 && contains(rsp, rl, "from s1"));
}

/* --- text commands ------------------------------------------------------- */

bool umcub_cmd_user(unsigned id, const char *text, umcub_cmd_reply_t reply)
{
    if (id == 7 && strcmp(text, "hello") == 0) {
        reply("hi\r\n");
        return true;
    }
    return false;
}

static bool stream_out_has(int i, const char *s)
{
    return contains((const uint8_t *)streams[i].out, streams[i].out_len, s);
}

static void test_commands(void)
{
    printf("[text commands] stream (immediate, garbage, unknown, user), packet, wait window\n");
    static const char in0[] = "i" "hello" "xyz\r" "zzb\n" "b";
    stream_polls = -1000000;    /* never longjmp out of this test */
    memset(streams, 0, sizeof(streams));
    streams[0].in = (const uint8_t *)in0;
    streams[0].in_len = sizeof(in0) - 1;
    streams[0].gate = sizeof(in0);
    (void)umcub_cmd_take_decision();
    /* not in recovery: "b" ends the wait window with "stay" */
    CHECK(umcub_recovery_wait(50) == true);
    CHECK(stream_out_has(0, "umcub ") && stream_out_has(0, "image 0 slot 1: "));
    CHECK(stream_out_has(0, "hi\r\n"));
    CHECK(stream_out_has(0, "? xyz"));
    CHECK(stream_out_has(0, "? zzb"));           /* not at line start: no "stay" from it */
    CHECK(stream_out_has(0, "ok stay"));

    static const char in1[] = "a";
    memset(streams, 0, sizeof(streams));
    streams[1].in = (const uint8_t *)in1;
    streams[1].in_len = 1;
    streams[1].gate = 1;
    CHECK(umcub_recovery_wait(50) == false);          /* "a" skips the rest of the window */
    CHECK(stream_out_has(1, "ok boot"));

    /* packet transport: a non-SMP packet is a command, reply as a packet */
    pkt_rsp_len = 0;
    CHECK(umcub_smp_packet_rx(&t_pkt, (const uint8_t *)"i\n", 2));
    int nl;
    char tmp[64];
    boot_uf = umcub_mux_funcs_for_test();
    boot_uf->read(tmp, sizeof(tmp), &nl);
    CHECK(contains(pkt_rsp, pkt_rsp_len, "umcub ") && contains(pkt_rsp, pkt_rsp_len, "image 0 slot 1"));
    memset(streams, 0, sizeof(streams));
    CHECK(umcub_recovery_wait(5) == false);            /* nothing pending */
}

/* --- slot inspection: verify / hash / read (text + SMP group) ------------- */

#include "bootutil/crypto/sha.h"

static void sha256(const uint8_t *p, size_t n, uint8_t out[32])
{
    bootutil_sha_context c;
    bootutil_sha_init(&c);
    bootutil_sha_update(&c, p, (uint32_t)n);
    bootutil_sha_finish(&c, out);
    bootutil_sha_drop(&c);
}

static void hexstr(const uint8_t *p, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) {
        sprintf(out + 2 * i, "%02x", p[i]);
    }
}

static void stream_cmd(const char *text)
{
    stream_polls = -1000000;
    memset(streams, 0, sizeof(streams));
    streams[0].in = (const uint8_t *)text;
    streams[0].in_len = strlen(text);
    streams[0].gate = strlen(text);
    (void)umcub_recovery_wait(30);
}

static void test_inspect(const uint8_t *img, size_t img_len)
{
    printf("[inspect] verify, hash (text + SMP group 100), read\n");
    uint8_t h[32];
    char hex[65], want[100];

    /* primary holds the confirmed image `img` (1.1.0) at this point */
    stream_cmd("verify 0 0\r");
    CHECK(stream_out_has(0, "ok valid"));
    stream_cmd("verify 0 7\r");
    CHECK(stream_out_has(0, "? bad image/slot"));


    sha256(img, img_len, h);
    hexstr(h, 32, hex);
    snprintf(want, sizeof(want), "sha256 %s len %zu", hex, img_len);
    stream_cmd("hash 0 0\r");
    CHECK(stream_out_has(0, want));
    /* TLV areas inconsistent with the header (ih_protect_tlv_size): no image length */
    uint8_t *pts = &fake_flash[UMCUB_CFG_IMG0_PRIMARY_ADDR - 0x08000000 + 10];
    *pts ^= 0x04;
    stream_cmd("hash 0 0\r");
    CHECK(stream_out_has(0, "? hash failed"));
    *pts ^= 0x04;
    sha256(img + 0x10, 0x100, h);
    hexstr(h, 32, hex);
    snprintf(want, sizeof(want), "sha256 %s len 256", hex);
    stream_cmd("hash 0 0 0x10 256\r");
#if UMCUB_CFG_ENCRYPT_IMAGES
    /* a hash over a chosen range is readback by other means (byte by byte) */
    CHECK(stream_out_has(0, "? only whole-image hashes outside an encrypted session"));
#else
    CHECK(stream_out_has(0, want));
#endif

    /* immediate mode must not fire on the command word alone */
    stream_cmd("verify");
    CHECK(streams[0].out_len == 0);
    stream_cmd("\r");                         /* Enter without arguments */
    CHECK(stream_out_has(0, "? usage"));

    hexstr(img, 8, hex);
    snprintf(want, sizeof(want), "00000000 %s", hex);
    stream_cmd("r");                           /* prefix of "read": must not fire yet */
    CHECK(streams[0].out_len == 0);
    stream_cmd("ead 0 0 0 8\r");
#if UMCUB_CFG_ENCRYPT_IMAGES
    /* installed images are plain text: no readback outside an encrypted session */
    CHECK(stream_out_has(0, "? readback only in an encrypted session"));
#else
    CHECK(stream_out_has(0, want));
#endif

    /* one flipped payload byte: verify must fail, hash must change */
    uint8_t *byte = &fake_flash[UMCUB_CFG_IMG0_PRIMARY_ADDR - 0x08000000 + 0x800];
    *byte ^= 0x40;
    stream_cmd("verify 0 0\r");
    CHECK(stream_out_has(0, "bad hash or signature"));
    *byte ^= 0x40;

    /* SMP group: hash request {image 0, slot 0} */
    cbor_t c = { .n = 0 };
    cb_map(&c, 2);
    cb_text(&c, "image");
    cb_uint(&c, 0);
    cb_text(&c, "slot");
    cb_uint(&c, 0);
    uint8_t pkt[64];
    size_t pl = smp_frame(pkt, 0, UMCUB_CFG_SMP_INSPECT_GROUP, 1, &c);
    smp_packet_request(pkt, pl);
    sha256(img, img_len, h);
    CHECK(pkt_rsp_len > 8 && cbor_find_uint(pkt_rsp + 8, pkt_rsp_len - 8, "rc") == 0 &&
          contains_bytes(pkt_rsp, pkt_rsp_len, h, 32));
    pl = smp_frame(pkt, 0, UMCUB_CFG_SMP_INSPECT_GROUP, 0, &c);   /* verify */
    smp_packet_request(pkt, pl);
    /* indefinite-length map: ... "valid" true 0xff */
    CHECK(pkt_rsp_len > 9 && contains(pkt_rsp, pkt_rsp_len, "valid") && pkt_rsp[pkt_rsp_len - 2] == 0xF5);
    pl = smp_frame(pkt, 0, UMCUB_CFG_SMP_INSPECT_GROUP, 9, &c);   /* unknown id */
    smp_packet_request(pkt, pl);
    CHECK(cbor_find_uint(pkt_rsp + 8, pkt_rsp_len - 8, "rc") == 8);
}

/* What the host sees in the secondary slot (text "info", "verify") while an
 * update is pending and after the test swap: in swap-offset the update and
 * the revert copy start at different offsets, as MCUboot reads them. */
static void test_secondary_views(const uint8_t *v2, size_t v2_len)
{
    char ver[16];
    printf("[inspect] secondary slot: pending update, revert copy\n");
    write_secondary(v2, v2_len);
    stream_cmd("i");
    CHECK(stream_out_has(0, "image 0 slot 1: 1.1.0+0 pending"));
    stream_cmd("verify 0 1\r");
    CHECK(stream_out_has(0, "ok valid"));
    CHECK(boot_ok(ver) && strcmp(ver, "1.1.0") == 0);      /* test swap */
    stream_cmd("i");
    CHECK(stream_out_has(0, "image 0 slot 0: 1.1.0+0 test") &&
          stream_out_has(0, "image 0 slot 1: 1.0.0+0"));
    stream_cmd("verify 0 1\r");
    CHECK(stream_out_has(0, "ok valid"));
    CHECK(boot_ok(ver) && strcmp(ver, "1.0.0") == 0);      /* not confirmed: revert */
}

/* --- ISO-TP over a fake CAN bus ----------------------------------------- */

static uint8_t can_sent[600][64];
static uint8_t can_sent_len[600];
static unsigned can_sent_n;
static uint8_t can_rxq[8][64];
static uint8_t can_rxq_len[8];
static unsigned can_rxq_n;
static unsigned cf_since_fc;
static const uint8_t fc_bs = 4;

int umcub_port_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    (void)id;
    memcpy(can_sent[can_sent_n], data, len);
    can_sent_len[can_sent_n++] = len;
    uint8_t type = data[0] >> 4;
    if (type == 1 || (type == 2 && ++cf_since_fc == fc_bs)) {
        cf_since_fc = 0;                        /* peer answers with FC CTS, BS = 4 */
        can_rxq[can_rxq_n][0] = 0x30;
        can_rxq[can_rxq_n][1] = fc_bs;
        can_rxq[can_rxq_n][2] = 0;
        can_rxq_len[can_rxq_n++] = 3;
    }
    return 0;
}

bool umcub_port_can_recv(uint32_t *id, uint8_t *data, uint8_t *len)
{
    if (!can_rxq_n) {
        return false;
    }
    *id = 0;
    memcpy(data, can_rxq[0], can_rxq_len[0]);
    *len = can_rxq_len[0];
    memmove(can_rxq, can_rxq + 1, sizeof(can_rxq[0]) * --can_rxq_n);
    memmove(can_rxq_len, can_rxq_len + 1, can_rxq_n);
    return true;
}

static void test_isotp(void)
{
    printf("[iso-tp] classic and FD, single and multi frame\n");
    static const size_t sizes[] = { 1, 7, 8, 62, 63, 64, 300, 1000 };
    for (int fd = 0; fd <= 1; fd++) {
        for (unsigned k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
            uint8_t msg[1024], rx[1024];
            for (size_t i = 0; i < sizes[k]; i++) {
                msg[i] = (uint8_t)(i * 7 + k);
            }
            isotp_t tx = { .frame_len = fd ? 64 : 8, .tx_id = 0x7C8 };
            isotp_t rcv = { .frame_len = fd ? 64 : 8, .tx_id = 0x7C0, .rx_buf = rx, .rx_cap = sizeof(rx) };
            can_sent_n = can_rxq_n = cf_since_fc = 0;
            CHECK(isotp_send(&tx, msg, sizes[k]) == 0);
            unsigned sent = can_sent_n;
            size_t got = 0;
            for (unsigned f = 0; f < sent; f++) {
                got = isotp_on_frame(&rcv, can_sent[f], can_sent_len[f]);
            }
            if (got != sizes[k] || memcmp(rx, msg, sizes[k]) != 0) {
                printf("  FAIL iso-tp fd=%d size %zu (got %zu)\n", fd, sizes[k], got);
                failures++;
            }
        }
    }
}

/* --- network stack over a fake MAC --------------------------------------- */

static uint8_t eth_out[16][1600];
static size_t eth_out_len[16];
static unsigned eth_out_n;

int umcub_port_eth_tx(const uint8_t *f, size_t len)
{
    memcpy(eth_out[eth_out_n % 16], f, len);
    eth_out_len[eth_out_n++ % 16] = len;
    return 0;
}

void net_udp_input(const net_peer_t *from, uint16_t dst_port, const uint8_t *data, size_t len)
{
    (void)from;
    (void)dst_port;
    (void)data;
    (void)len;
}

static const uint8_t host_mac[6] = { 0x02, 0, 0, 0, 0, 0x01 };

static size_t ip_udp(uint8_t *f, uint32_t src, uint32_t dst, uint16_t sp, uint16_t dp, const uint8_t *pl, size_t len,
                     const uint8_t *dst_mac)
{
    memcpy(f, dst_mac, 6);
    memcpy(f + 6, host_mac, 6);
    net_put16(f + 12, 0x0800);
    uint8_t *ip = f + 14;
    memset(ip, 0, 28);
    ip[0] = 0x45;
    net_put16(ip + 2, (uint16_t)(28 + len));
    ip[8] = 64;
    ip[9] = 17;
    net_put32(ip + 12, src);
    net_put32(ip + 16, dst);
    uint32_t s = 0;
    for (int i = 0; i < 20; i += 2) s += net_get16(ip + i);
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    net_put16(ip + 10, (uint16_t)~s);
    net_put16(ip + 20, sp);
    net_put16(ip + 22, dp);
    net_put16(ip + 24, (uint16_t)(8 + len));
    memcpy(ip + 28, pl, len);       /* UDP checksum 0 = none */
    return 14 + 28 + len;
}

static void test_net(void)
{
    printf("[net] DHCP discover/offer/request/ack, ARP reply, ICMP echo\n");
    const uint8_t mac[6] = { 0x02, 0x55, 1, 2, 3, 4 };
    net_init(mac);
    eth_out_n = 0;
    dhcp_start();
    dhcp_tick();
    CHECK(eth_out_n == 1);
    const uint8_t *d = eth_out[0] + 42;     /* DISCOVER payload */
    CHECK(eth_out_len[0] >= 42 + 240 && net_get16(eth_out[0] + 36) == 67);
    uint32_t xid = net_get32(d + 4);

    uint8_t bootp[300] = { 2, 1, 6, 0 };
    net_put32(bootp + 4, xid);
    net_put32(bootp + 16, 0xC0A80A2A);      /* yiaddr 192.168.10.42 */
    memcpy(bootp + 28, mac, 6);
    net_put32(bootp + 236, 0x63825363);
    uint8_t opts[] = { 53, 1, 2, 54, 4, 192, 168, 10, 1, 1, 4, 255, 255, 255, 0, 51, 4, 0, 0, 0x0E, 0x10, 255 };
    memcpy(bootp + 240, opts, sizeof(opts));
    uint8_t f[1600];
    static const uint8_t bc[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    size_t fl = ip_udp(f, 0xC0A80A01, 0xFFFFFFFF, 67, 68, bootp, sizeof(bootp), bc);
    net_input(f, fl);                       /* OFFER -> REQUEST */
    CHECK(eth_out_n == 2 && eth_out[1][42 + 240 + 2] == 3);
    bootp[240 + 2] = 5;                     /* ACK */
    fl = ip_udp(f, 0xC0A80A01, 0xFFFFFFFF, 67, 68, bootp, sizeof(bootp), bc);
    net_input(f, fl);
    CHECK(net_if.configured && net_if.ip == 0xC0A80A2A && net_if.netmask == 0xFFFFFF00);

    /* ARP who-has 192.168.10.42 */
    uint8_t arp[42];
    memset(arp, 0xFF, 6);
    memcpy(arp + 6, host_mac, 6);
    net_put16(arp + 12, 0x0806);
    uint8_t a[28] = { 0, 1, 8, 0, 6, 4, 0, 1 };
    memcpy(a + 8, host_mac, 6);
    net_put32(a + 14, 0xC0A80A64);
    net_put32(a + 24, 0xC0A80A2A);
    memcpy(arp + 14, a, 28);
    unsigned before = eth_out_n;
    net_input(arp, sizeof(arp));
    CHECK(eth_out_n == before + 1);
    const uint8_t *r = eth_out[before % 16];
    CHECK(net_get16(r + 12) == 0x0806 && net_get16(r + 20) == 2 && memcmp(r, host_mac, 6) == 0 &&
          memcmp(r + 22, mac, 6) == 0);

    /* ICMP echo */
    uint8_t ping[14 + 20 + 16];
    memcpy(ping, mac, 6);
    memcpy(ping + 6, host_mac, 6);
    net_put16(ping + 12, 0x0800);
    uint8_t *ip = ping + 14;
    memset(ip, 0, 36);
    ip[0] = 0x45;
    net_put16(ip + 2, 36);
    ip[8] = 64;
    ip[9] = 1;
    net_put32(ip + 12, 0xC0A80A64);
    net_put32(ip + 16, 0xC0A80A2A);
    uint32_t s = 0;
    for (int i = 0; i < 20; i += 2) s += net_get16(ip + i);
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    net_put16(ip + 10, (uint16_t)~s);
    uint8_t *ic = ip + 20;
    ic[0] = 8;
    memcpy(ic + 8, "pingpong", 8);
    s = 0;
    for (int i = 0; i < 16; i += 2) s += net_get16(ic + i);
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    net_put16(ic + 2, (uint16_t)~s);
    before = eth_out_n;
    net_input(ping, sizeof(ping));
    CHECK(eth_out_n == before + 1);
    r = eth_out[before % 16];
    CHECK(net_get16(r + 12) == 0x0800 && r[14 + 9] == 1 && r[34] == 0 && memcmp(r + 42, "pingpong", 8) == 0);
}

/* ------------------------------------------------------------------------ */

static void log_sink(const char *s, size_t len)
{
    fwrite(s, 1, len, stdout);
}

int main(int argc, char **argv)
{
    int want_args = UMCUB_CFG_ENCRYPT_IMAGES ? 6 : 4;
    if (argc != want_args) {
        fprintf(stderr, "usage: %s v1.signed.bin v2.signed.bin foreign.signed.bin%s\n", argv[0],
                UMCUB_CFG_ENCRYPT_IMAGES ? " v1.plain.bin v2.plain.bin (first three encrypted)" : "");
        return 2;
    }
    size_t v1_len, v2_len, foreign_len, plain_len;
    uint8_t *v1 = load(argv[1], &v1_len);
    uint8_t *v2 = load(argv[2], &v2_len);
    uint8_t *foreign = load(argv[3], &foreign_len);
    const uint8_t *v1_flash = in_flash(v1, v1_len, argc > 4 ? load(argv[4], &plain_len) : NULL);
    const uint8_t *v2_flash = in_flash(v2, v2_len, argc > 5 ? load(argv[5], &plain_len) : NULL);
    setvbuf(stdout, NULL, _IONBF, 0);
    umcub_log_set_sink(log_sink);

    test_packet_upload_and_boot(v1, v1_len, v1_flash);
    test_secondary_views(v2, v2_len);
    test_swap_revert_confirm(v2, v2_len);
    test_board_type(foreign, foreign_len);
    test_inspect(v2_flash, v2_len);
    test_streams_interleaved(v1, v1_len);
    test_commands();
    test_isotp();
    test_net();

    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
