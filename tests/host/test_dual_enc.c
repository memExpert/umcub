/*
 * umcub host test: encrypted SMP uploads with two images (H755 SINGLE_BOOT,
 * swap-scratch). An encrypted upload into the primary slot of either image is
 * decrypted in place (image 1+: MCUboot v2.5.0, mcu-tools/mcuboot#2912); with
 * direct upload image 1 also goes through its secondary slot (state-write, swap).
 * A test upgrade of image 0 next to a missing or broken image 1 starts the
 * new image 0 and is reverted on the next boot, not at once.
 *   test_dual_enc <image0 signed> <image0 encrypted> <image1 2.0.0 signed> <image1 2.1.0 signed> <image1 2.1.0 encrypted>
 *                 <image0 2.1.0 signed>
 */
#include <setjmp.h>
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
#include "bootutil/bootutil_public.h"
#include "boot_serial/boot_serial.h"
#include "umcub_image_info.h"

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

/* Image 0 2.0.0 running, 2.1.0 in its secondary slot for a test boot, image 1 as `img1` (NULL: none): the first boot
 * starts 2.1.0 (image 1 not), the next one without a confirmation reverts to 2.0.0. */
static void image0_test_alone(const char *what, const uint8_t *p0, size_t p0n, const uint8_t *v1, size_t v1n,
                              const uint8_t *img1, size_t img1n, unsigned cut)
{
    printf("[two images] test upgrade of image 0, image 1 %s: 2.1.0 starts, the next boot reverts\n", what);
    fake_flash_reset();
    memcpy(fake_flash + (UMCUB_CFG_IMG0_PRIMARY_ADDR - 0x08000000u), p0, p0n);
    memcpy(fake_flash + (UMCUB_CFG_IMG0_SECONDARY_ADDR - 0x08000000u), v1, v1n);
    if (img1) {
        memcpy(fake_flash + (UMCUB_CFG_IMG1_PRIMARY_ADDR - 0x08000000u), img1, img1n);
    }
    CHECK(boot_set_pending_multi(0, 0) == 0);
    umcub_image0_updated = false;                       /* power on */
    struct boot_rsp r;
    bool all = true;
    FIH_DECLARE(rc, FIH_FAILURE);
    if (cut) {                                          /* the power cut in the middle of the swap */
        jmp_buf jb;
        fake_erase_cut = cut;
        fake_reset_jmp = &jb;
        if (!setjmp(jb)) {
            FIH_CALL(umcub_boot_go, rc, &r, &all);
            CHECK(!"the swap was not cut");
        }
        fake_reset_jmp = NULL;
        fake_erase_cut = 0;
    }
    umcub_image0_updated = false;                       /* a fresh boot: it completes the cut swap */
    FIH_CALL(umcub_boot_go, rc, &r, &all);
    CHECK(FIH_EQ(rc, FIH_SUCCESS) && !all && r.br_hdr->ih_ver.iv_minor == 1 && umcub_image0_updated);
    CHECK(r.br_image_off == UMCUB_CFG_IMG0_PRIMARY_ADDR);
    all = true;
    umcub_image0_updated = false;
    FIH_CALL(umcub_boot_go, rc, &r, &all);              /* reset, not confirmed */
    CHECK(FIH_EQ(rc, FIH_SUCCESS) && !all && r.br_hdr->ih_ver.iv_minor == 0 && umcub_image0_updated);
}

static bool both_boot(void)
{
    struct boot_rsp r;
    FIH_DECLARE(rc, FIH_FAILURE);
    FIH_CALL(boot_go, rc, &r);
    return FIH_EQ(rc, FIH_SUCCESS);
}

int main(int argc, char **argv)
{
    if (argc != 7) {
        fprintf(stderr, "usage: %s image0 image0.encrypted image1-2.0 image1-2.1 image1-2.1.encrypted image0-2.1\n",
                argv[0]);
        return 2;
    }
    size_t p0n, e0n, p1n, q1n, e1n;
    uint8_t *p0 = load(argv[1], &p0n), *e0 = load(argv[2], &e0n), *p1 = load(argv[3], &p1n);
    uint8_t *q1 = load(argv[4], &q1n), *e1 = load(argv[5], &e1n);   /* 2.1.0: plain reference, encrypted */
    (void)q1n;
    size_t v0n;
    uint8_t *v0 = load(argv[6], &v0n);                 /* image 0 2.1.0 */
    setvbuf(stdout, NULL, _IONBF, 0);
    image0_test_alone("missing", p0, p0n, v0, v0n, NULL, 0, 0);
    image0_test_alone("missing, the swap cut by a power loss", p0, p0n, v0, v0n, NULL, 0, 4);  /* of 9 */
    p1[umcub_get_le16(p1 + 8) + 100] ^= 0xFFu;          /* image 1 present, its hash wrong */
    image0_test_alone("broken", p0, p0n, v0, v0n, p1, p1n, 0);
    p1[umcub_get_le16(p1 + 8) + 100] ^= 0xFFu;
    fake_flash_reset();
    boot_uf = umcub_mux_funcs_for_test();
#if !UMCUB_CFG_SMP_DIRECT_UPLOAD
    printf("[two images] encrypted uploads into the primary slots of image 0 and image 1 decrypted\n");
    CHECK(upload(e0, e0n, 0) == 0);                     /* image 0: decrypted in place */
    CHECK(plain_in(UMCUB_CFG_IMG0_PRIMARY_ADDR, p0));
    CHECK(upload(p1, p1n, 1) == 0);                     /* a plain image 1 is fine */
    CHECK(both_boot());
    CHECK(upload(e1, e1n, 1) == 0);                     /* encrypted image 1: decrypted in place too */
    CHECK(plain_in(UMCUB_CFG_IMG1_PRIMARY_ADDR, q1));
    CHECK(both_boot());
#else
    printf("[two images, direct upload] image 0 as 1, image 1 secondary (4) + swap, primary (3) decrypted\n");
    CHECK(upload(e0, e0n, 1) == 0);                     /* "image" 1 = image 0's primary slot */
    CHECK(plain_in(UMCUB_CFG_IMG0_PRIMARY_ADDR, p0));
    CHECK(upload(p1, p1n, 3) == 0);
    CHECK(both_boot());
    CHECK(upload(e1, e1n, 4) == 0);                     /* image 1's secondary slot */
    const uint8_t *h = hash_tlv(e1, e1n);
    CHECK(h && state_write_test(h) == 0);
    CHECK(primary_minor(UMCUB_CFG_IMG1_PRIMARY_ADDR) == 0);
    CHECK(both_boot());                                 /* swap: 2.1.0 decrypted into the primary slot */
    CHECK(primary_minor(UMCUB_CFG_IMG1_PRIMARY_ADDR) == 1);
    CHECK(plain_in(UMCUB_CFG_IMG1_PRIMARY_ADDR, q1));
    CHECK(upload(e1, e1n, 3) == 0);                     /* image 1's primary slot: decrypted in place */
    CHECK(plain_in(UMCUB_CFG_IMG1_PRIMARY_ADDR, q1));
    CHECK(both_boot());
#endif
    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
