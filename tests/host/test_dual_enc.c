/*
 * umcub host test: encrypted SMP uploads with two images (H755 SINGLE_BOOT,
 * swap-scratch). MCUboot decrypts an upload into the primary slot of image 0
 * only (mcu-tools/mcuboot#2910), so umcub refuses an encrypted upload into the
 * primary slot of image 1 (rc 3) instead of keeping the ciphertext; image 1
 * goes through its secondary slot (direct upload + state-write, swap).
 *   test_dual_enc <image0 signed> <image0 encrypted> <image1 2.0.0 signed> <image1 2.1.0 signed> <image1 2.1.0 encrypted>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fake_port.h"
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "umcub_proto.h"
#include "umcub_bytes.h"
#include "umcub_handoff.h"
#include "umcub_cmd.h"
#include "bootutil/bootutil.h"
#include "bootutil/image.h"
#include "bootutil/fault_injection_hardening.h"
#include "boot_serial/boot_serial.h"

extern const struct boot_uart_funcs *boot_uf;
const struct boot_uart_funcs *umcub_mux_funcs_for_test(void);

static int failures;
#define CHECK(c)                                                                 \
    do {                                                                         \
        if (!(c)) {                                                              \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static uint8_t rsp[2048];
static size_t rsp_len;
static int nop_init(void) { return 0; }
static void nop_deinit(void) {}
static int pkt_send(const uint8_t *p, size_t l)
{
    memcpy(rsp, p, l);
    rsp_len = l;
    return 0;
}
static const umcub_transport_t t_pkt = {
    .id = UMCUB_TRANSPORT_ETH, .init = nop_init, .deinit = nop_deinit, .send_packet = pkt_send,
};
const umcub_transport_t *const umcub_transports[] = { &t_pkt, 0 };
const unsigned umcub_transport_count = 1;
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

static uint8_t *load(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    rewind(f);
    uint8_t *b = malloc(*len);
    if (fread(b, 1, *len, f) != *len) {
        exit(2);
    }
    fclose(f);
    return b;
}

/* --- minimal CBOR and SMP ------------------------------------------------ */
static size_t put_hdr(uint8_t *o, uint8_t major, uint32_t v)
{
    if (v < 24) {
        o[0] = (uint8_t)(major << 5 | v);
        return 1;
    }
    if (v < 256) {
        o[0] = (uint8_t)(major << 5 | 24);
        o[1] = (uint8_t)v;
        return 2;
    }
    if (v < 65536) {
        o[0] = (uint8_t)(major << 5 | 25);
        umcub_put_be16(o + 1, (uint16_t)v);
        return 3;
    }
    o[0] = (uint8_t)(major << 5 | 26);
    umcub_put_be32(o + 1, v);
    return 5;
}
static size_t put_text(uint8_t *o, const char *s)
{
    size_t n = put_hdr(o, 3, (uint32_t)strlen(s));
    memcpy(o + n, s, strlen(s));
    return n + strlen(s);
}

static void request(uint8_t op, uint8_t group, uint8_t id, const uint8_t *body, size_t len)
{
    static uint8_t seq;
    uint8_t p[1100];
    p[0] = op;
    p[1] = 0;
    umcub_put_be16(p + 2, (uint16_t)len);
    umcub_put_be16(p + 4, group);
    p[6] = seq++;
    p[7] = id;
    memcpy(p + 8, body, len);
    rsp_len = 0;
    CHECK(umcub_smp_packet_rx(&t_pkt, p, 8 + len));
    int nl;
    char tmp[1100];
    boot_uf->read(tmp, sizeof(tmp), &nl);
}

/* "rc" of the last response (0 when absent). */
static long rsp_rc(void)
{
    for (size_t i = 8; i + 3 < rsp_len; i++) {
        if (rsp[i] == 0x62 && rsp[i + 1] == 'r' && rsp[i + 2] == 'c') {
            uint8_t v = rsp[i + 3];
            return v < 24 ? v : v == 24 ? rsp[i + 4] : -1;
        }
    }
    return 0;
}

/* SMP upload in 512-byte chunks; returns the rc of the last response. */
static long upload(const uint8_t *img, size_t len, int image)
{
    for (size_t off = 0; off < len;) {
        size_t n = len - off > 512 ? 512 : len - off;
        uint8_t b[1100];
        size_t k = put_hdr(b, 5, off == 0 ? 4 : 3);
        k += put_text(b + k, "image");
        k += put_hdr(b + k, 0, (uint32_t)image);
        if (off == 0) {
            k += put_text(b + k, "len");
            k += put_hdr(b + k, 0, (uint32_t)len);
        }
        k += put_text(b + k, "off");
        k += put_hdr(b + k, 0, (uint32_t)off);
        k += put_text(b + k, "data");
        k += put_hdr(b + k, 2, (uint32_t)n);
        memcpy(b + k, img + off, n);
        request(2, 1, 1, b, k + n);
        CHECK(rsp_len > 8);
        long rc = rsp_rc();
        if (rc != 0) {
            return rc;
        }
        off += n;
    }
    return 0;
}

#if UMCUB_CFG_SMP_DIRECT_UPLOAD
/* The SHA-256 TLV of a signed image: what image state-write names. */
static const uint8_t *hash_tlv(const uint8_t *img, size_t len)
{
    size_t off = umcub_get_le16(img + 8) + umcub_get_le32(img + 12);
    for (int pass = 0; pass < 2 && off + 4 <= len; pass++) {
        size_t end = off + umcub_get_le16(img + off + 2);
        for (size_t p = off + 4; p + 4 <= end && end <= len;) {
            uint16_t type = umcub_get_le16(img + p), l = umcub_get_le16(img + p + 2);
            if (type == IMAGE_TLV_SHA256 && l == 32) {
                return img + p + 4;
            }
            p += 4u + l;
        }
        off = end;
    }
    return NULL;
}

static long state_write_test(const uint8_t *hash)
{
    uint8_t b[64];
    size_t k = put_hdr(b, 5, 2);
    k += put_text(b + k, "hash");
    k += put_hdr(b + k, 2, 32);
    memcpy(b + k, hash, 32);
    k += 32;
    k += put_text(b + k, "confirm");
    b[k++] = 0xF4;                          /* false: test */
    request(2, 1, 0, b, k);
    return rsp_rc();
}
#endif

static bool plain_in(uint32_t slot_addr, const uint8_t *plain)
{
    uint32_t hdr = umcub_get_le16(plain + 8), size = umcub_get_le32(plain + 12);
    return memcmp(fake_flash + (slot_addr - 0x08000000u) + hdr, plain + hdr, size) == 0;
}

#if UMCUB_CFG_SMP_DIRECT_UPLOAD
static unsigned primary_minor(uint32_t slot_addr)
{
    struct image_header h;
    memcpy(&h, fake_flash + (slot_addr - 0x08000000u), sizeof(h));
    return h.ih_ver.iv_minor;
}
#endif

static bool both_boot(void)
{
    struct boot_rsp r;
    FIH_DECLARE(rc, FIH_FAILURE);
    FIH_CALL(boot_go, rc, &r);
    return FIH_EQ(rc, FIH_SUCCESS);
}

int main(int argc, char **argv)
{
    if (argc != 6) {
        fprintf(stderr, "usage: %s image0 image0.encrypted image1-2.0 image1-2.1 image1-2.1.encrypted\n", argv[0]);
        return 2;
    }
    size_t p0n, e0n, p1n, q1n, e1n;
    uint8_t *p0 = load(argv[1], &p0n), *e0 = load(argv[2], &e0n), *p1 = load(argv[3], &p1n);
    uint8_t *q1 = load(argv[4], &q1n), *e1 = load(argv[5], &e1n);   /* 2.1.0: plain reference, encrypted */
    (void)q1n;
    (void)q1;
    setvbuf(stdout, NULL, _IONBF, 0);
    fake_flash_reset();
    boot_uf = umcub_mux_funcs_for_test();
#if !UMCUB_CFG_SMP_DIRECT_UPLOAD
    printf("[two images] encrypted upload into image 1's primary slot refused (MCUboot would not decrypt it)\n");
    CHECK(upload(e0, e0n, 0) == 0);                     /* image 0: decrypted in place */
    CHECK(plain_in(UMCUB_CFG_IMG0_PRIMARY_ADDR, p0));
    CHECK(upload(p1, p1n, 1) == 0);                     /* a plain image 1 is fine */
    CHECK(both_boot());
    CHECK(upload(e1, e1n, 1) == 3);                     /* encrypted image 1: refused at the last chunk */
#else
    printf("[two images, direct upload] image 0 as 1, image 1 primary (3) refused, secondary (4) + swap\n");
    CHECK(upload(e0, e0n, 1) == 0);                     /* "image" 1 = image 0's primary slot */
    CHECK(plain_in(UMCUB_CFG_IMG0_PRIMARY_ADDR, p0));
    CHECK(upload(p1, p1n, 3) == 0);
    CHECK(both_boot());
    CHECK(upload(e1, e1n, 3) == 3);                     /* image 1's primary slot: refused */
    CHECK(upload(p1, p1n, 3) == 0);                     /* back to a bootable image 1 */
    CHECK(upload(e1, e1n, 4) == 0);                     /* image 1's secondary slot */
    const uint8_t *h = hash_tlv(e1, e1n);
    CHECK(h && state_write_test(h) == 0);
    CHECK(primary_minor(UMCUB_CFG_IMG1_PRIMARY_ADDR) == 0);
    CHECK(both_boot());                                 /* swap: 2.1.0 decrypted into the primary slot */
    CHECK(primary_minor(UMCUB_CFG_IMG1_PRIMARY_ADDR) == 1 && plain_in(UMCUB_CFG_IMG1_PRIMARY_ADDR, q1));
#endif
    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
