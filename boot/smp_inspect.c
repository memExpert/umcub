/*
 * SMP group UMCUB_CFG_SMP_INSPECT_GROUP (MCUboot "per-user" group hook):
 *   id 0 verify {image, slot}            -> {rc, valid: bool}
 *   id 1 hash   {image, slot, off?, len?} -> {rc, sha: bstr[32], len}
 *   id 2 read   {image, slot, off, len}   -> {rc, off, data: bstr}   (UMCUB_CFG_READBACK)
 * rc: 0 ok, 3 EINVAL, 5 ENOENT (no image), 8 ENOTSUP. Host: tools/umcub_inspect.py.
 */
#include <string.h>
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "umcub_inspect.h"
#include "mcuboot_config/mcuboot_config.h"

#if UMCUB_CFG_INSPECT

#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include "boot_serial/boot_serial.h"
#include "boot_serial_priv.h"
#include "zcbor_bulk.h"

#ifndef zcbor_tstr_put_lit_cast   /* defined privately in boot_serial.c */
#define zcbor_tstr_put_lit_cast(state, string) zcbor_tstr_encode_ptr(state, (char *)string, sizeof(string) - 1)
#endif

#define ERR_OK      0
#define ERR_EINVAL  3
#define ERR_ENOENT  5
#define ERR_ENOTSUP 8
#define READ_MAX    128u     /* fits boot_serial's response buffer */

static int map_rc(int rc)
{
    return rc == 0 ? ERR_OK : rc == UMCUB_ENOTSUP ? ERR_ENOENT : rc == UMCUB_EINVAL ? ERR_EINVAL : ERR_EINVAL;
}

static void put_rc(zcbor_state_t *cs, int rc)
{
    zcbor_tstr_put_lit_cast(cs, "rc");
    zcbor_int32_put(cs, rc);
}

int bs_peruser_system_specific(const struct nmgr_hdr *hdr, const char *buffer, int len, zcbor_state_t *cs)
{
    uint32_t image = 0, slot = 0, off = 0, rlen = 0;
    size_t decoded = 0;
    zcbor_state_t zsd[4];

    zcbor_map_start_encode(cs, 10);
    if (hdr->nh_group != UMCUB_CFG_SMP_INSPECT_GROUP) {
        put_rc(cs, ERR_ENOTSUP);
        zcbor_map_end_encode(cs, 10);
        return 0;
    }

    zcbor_new_decode_state(zsd, sizeof(zsd) / sizeof(zsd[0]), (const uint8_t *)buffer, (size_t)len, 1, NULL, 0);
    struct zcbor_map_decode_key_val req[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("image", zcbor_uint32_decode, &image),
        ZCBOR_MAP_DECODE_KEY_DECODER("slot", zcbor_uint32_decode, &slot),
        ZCBOR_MAP_DECODE_KEY_DECODER("off", zcbor_uint32_decode, &off),
        ZCBOR_MAP_DECODE_KEY_DECODER("len", zcbor_uint32_decode, &rlen),
    };
    if (len > 0 && zcbor_map_decode_bulk(zsd, req, sizeof(req) / sizeof(req[0]), &decoded) != 0) {
        put_rc(cs, ERR_EINVAL);
        zcbor_map_end_encode(cs, 10);
        return 0;
    }

    switch (hdr->nh_id) {
#if UMCUB_CFG_INSPECT_VERIFY
    case 0: {
        int rc = umcub_inspect_verify((int)image, (int)slot);
        put_rc(cs, rc == UMCUB_EIO ? ERR_OK : map_rc(rc));
        zcbor_tstr_put_lit_cast(cs, "valid");
        zcbor_bool_put(cs, rc == 0);
        break;
    }
#endif
#if UMCUB_CFG_INSPECT_HASH
    case 1: {
        uint8_t h[32];
        uint32_t hashed = 0;
        int rc = umcub_inspect_hash((int)image, (int)slot, off, rlen, h, &hashed);
        put_rc(cs, map_rc(rc));
        if (rc == 0) {
            zcbor_tstr_put_lit_cast(cs, "sha");
            zcbor_bstr_encode_ptr(cs, (const char *)h, sizeof(h));
            zcbor_tstr_put_lit_cast(cs, "len");
            zcbor_uint32_put(cs, hashed);
        }
        break;
    }
#endif
#if UMCUB_CFG_READBACK
    case 2: {
        uint8_t data[READ_MAX];
        if (rlen > READ_MAX) {
            rlen = READ_MAX;
        }
        int rc = umcub_inspect_read((int)image, (int)slot, off, data, rlen);
        put_rc(cs, map_rc(rc));
        if (rc == 0) {
            zcbor_tstr_put_lit_cast(cs, "off");
            zcbor_uint32_put(cs, off);
            zcbor_tstr_put_lit_cast(cs, "data");
            zcbor_bstr_encode_ptr(cs, (const char *)data, rlen);
        }
        break;
    }
#endif
    default:
        put_rc(cs, ERR_ENOTSUP);
        break;
    }
    zcbor_map_end_encode(cs, 10);
    return 0;
}

#endif /* UMCUB_CFG_INSPECT */
