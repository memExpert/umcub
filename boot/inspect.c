/*
 * Slot inspection for hosts: verify / hash / readback. See umcub_inspect.h.
 */
#include <string.h>
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "umcub_inspect.h"
#include "bootutil/boot_hooks.h"
#include "bootutil/bootutil.h"
#include "bootutil/image.h"
#include "bootutil/crypto/sha.h"
#include "bootutil/fault_injection_hardening.h"
#include "sysflash/sysflash.h"
#include "bootutil_priv.h"   /* loader state: sector maps for size checks */

static __attribute__((unused)) bool has_image(const struct flash_area *fa)
{
    struct image_header h;
    return flash_area_read(fa, 0, &h, sizeof(h)) == 0 && h.ih_magic == IMAGE_MAGIC;
}

int umcub_inspect_area(int image, int slot, struct flash_area *out)
{
    const struct flash_area *fa;
    if (image < 0 || image >= UMCUB_CFG_IMAGE_NUMBER || slot < 0 || slot > 1 ||
        flash_area_open((uint8_t)flash_area_id_from_multi_image_slot(image, slot), &fa)) {
        return UMCUB_EINVAL;
    }
    *out = *fa;
#if UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_SWAP_OFFSET
    struct flash_sector s;
    if (slot == 1 && !has_image(out) && flash_area_get_sector(fa, 0, &s) == 0) {
        out->fa_off += s.fs_size;
        out->fa_size -= s.fs_size;
    }
#endif
    return 0;
}

uint32_t umcub_inspect_image_len(const struct flash_area *fa)
{
    struct image_header h;
    struct image_tlv_info info;
    if (flash_area_read(fa, 0, &h, sizeof(h)) || h.ih_magic != IMAGE_MAGIC) {
        return 0;
    }
    uint32_t off = (uint32_t)h.ih_hdr_size + h.ih_img_size;
    uint32_t len = off;
    /* optional protected TLV area, then the unprotected one */
    for (int i = 0; i < 2; i++) {
        if (len + sizeof(info) > fa->fa_size || flash_area_read(fa, len, &info, sizeof(info))) {
            break;
        }
        if (info.it_magic == IMAGE_TLV_PROT_INFO_MAGIC || info.it_magic == IMAGE_TLV_INFO_MAGIC) {
            len += info.it_tlv_tot;
            if (info.it_magic == IMAGE_TLV_INFO_MAGIC) {
                break;
            }
        } else {
            break;
        }
    }
    return len <= fa->fa_size ? len : fa->fa_size;
}

int umcub_inspect_verify(int image, int slot)
{
    struct flash_area fa;
    struct image_header h;
    static uint8_t tmp[256];
    int rc = umcub_inspect_area(image, slot, &fa);
    if (rc) {
        return rc;
    }
    if (flash_area_read(&fa, 0, &h, sizeof(h)) || h.ih_magic != IMAGE_MAGIC) {
        return UMCUB_ENOTSUP;
    }
    /* bootutil_img_validate() checks the image size against the slot
     * geometry kept in the loader state: prepare it like boot_serial does. */
    struct boot_loader_state *st = boot_get_loader_state();
    boot_state_init(st);
    if (boot_open_all_flash_areas(st)) {
        boot_state_clear(st);
        return UMCUB_EIO;
    }
#if !defined(MCUBOOT_DIRECT_XIP) && !defined(MCUBOOT_RAM_LOAD)
    /* swap modes size images by sector maps (direct-xip: by the area) */
#if UMCUB_CFG_IMAGE_NUMBER > 1
    for (int i = 0; i < UMCUB_CFG_IMAGE_NUMBER; i++) {
        BOOT_CURR_IMG(st) = (uint8_t)i;
        (void)boot_read_sectors(st, NULL);
    }
#else
    (void)boot_read_sectors(st, NULL);
#endif
#endif
#if UMCUB_CFG_IMAGE_NUMBER > 1
    BOOT_CURR_IMG(st) = (uint8_t)image;
#endif
    FIH_DECLARE(fih_rc, FIH_FAILURE);
    FIH_CALL(bootutil_img_validate, fih_rc, st, &h, &fa, tmp, sizeof(tmp), NULL, 0, NULL);
#if UMCUB_CFG_BOARD_TYPE != 0
    /* Same rule as at boot: an image for another board type is not valid here. */
    if (FIH_EQ(fih_rc, FIH_SUCCESS)) {
        FIH_CALL(boot_image_check_hook, fih_rc, image, slot);
        if (FIH_EQ(fih_rc, FIH_BOOT_HOOK_REGULAR)) {
            fih_rc = FIH_SUCCESS;
        }
    }
#endif
    boot_close_all_flash_areas(st);
    boot_state_clear(st);
    return FIH_EQ(fih_rc, FIH_SUCCESS) ? 0 : UMCUB_EIO;
}

int umcub_inspect_hash(int image, int slot, uint32_t off, uint32_t len, uint8_t out[32], uint32_t *hashed_len)
{
    struct flash_area fa;
    uint8_t buf[256];
    int rc = umcub_inspect_area(image, slot, &fa);
    if (rc) {
        return rc;
    }
    if (len == 0) {
        len = umcub_inspect_image_len(&fa);
        if (len == 0) {
            return UMCUB_ENOTSUP;
        }
        len = len > off ? len - off : 0;
    }
    if (off > fa.fa_size || len > fa.fa_size - off) {
        return UMCUB_EINVAL;
    }
    bootutil_sha_context ctx;
    bootutil_sha_init(&ctx);
    for (uint32_t done = 0; done < len;) {
        uint32_t n = len - done > sizeof(buf) ? sizeof(buf) : len - done;
        if (flash_area_read(&fa, off + done, buf, n)) {
            bootutil_sha_drop(&ctx);
            return UMCUB_EIO;
        }
        bootutil_sha_update(&ctx, buf, n);
        done += n;
        if ((done & 0xFFFFu) == 0) {
            umcub_port_wdg_feed();
        }
    }
    bootutil_sha_finish(&ctx, out);
    bootutil_sha_drop(&ctx);
    if (hashed_len) {
        *hashed_len = len;
    }
    return 0;
}

int umcub_inspect_read(int image, int slot, uint32_t off, void *buf, uint32_t len)
{
#if UMCUB_CFG_READBACK
    struct flash_area fa;
    int rc = umcub_inspect_area(image, slot, &fa);
    if (rc) {
        return rc;
    }
    if (off > fa.fa_size || len > fa.fa_size - off) {
        return UMCUB_EINVAL;
    }
    return flash_area_read(&fa, off, buf, len) ? UMCUB_EIO : 0;
#else
    (void)image;
    (void)slot;
    (void)off;
    (void)buf;
    (void)len;
    return UMCUB_EINVAL;
#endif
}
