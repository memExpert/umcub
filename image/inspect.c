/*
 * Slot inspection for hosts: verify / hash / readback. See umcub_inspect.h.
 */
#include <string.h>
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "umcub_inspect.h"
#include "umcub_image_info.h"
#include "bootutil/boot_hooks.h"
#include "bootutil/bootutil.h"
#include "bootutil/image.h"
#include "bootutil/crypto/sha.h"
#include "bootutil/fault_injection_hardening.h"
#include "sysflash/sysflash.h"
#include "bootutil_priv.h"   /* loader state: sector maps for size checks */
#if UMCUB_CFG_ENCRYPT_IMAGES
#include "boot_serial/boot_serial_encryption.h"
#endif

int umcub_inspect_area(int image, int slot, struct flash_area *out)
{
    const struct flash_area *fa;
    if (image < 0 || image >= UMCUB_CFG_IMAGE_NUMBER || slot < 0 || slot > 1 ||
        flash_area_open((uint8_t)flash_area_id_from_multi_image_slot(image, slot), &fa)) {
        return UMCUB_EINVAL;
    }
    *out = *fa;
#if UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_SWAP_OFFSET
    /* Same rule as MCUboot (boot_serial bs_list, loader): an update starts at
     * the second sector of the secondary slot; only the previous image kept
     * for a revert (after a test swap) starts at offset 0. */
    struct flash_sector s;
    if (slot == 1 && boot_swap_type_multi(image) != BOOT_SWAP_TYPE_REVERT &&
        flash_area_get_sector(fa, 0, &s) == 0) {
        out->fa_off += s.fs_size;
        out->fa_size -= s.fs_size;
    }
#endif
    return 0;
}

/* Header + payload + TLV areas (MCUboot's TLV iterator checks the protected
 * area against ih_protect_tlv_size). 0 = no (consistent) image. */
uint32_t umcub_inspect_image_len(const struct flash_area *fa)
{
    struct image_header h;
    struct image_tlv_iter it;
    memset(&it, 0, sizeof(it));
    if (flash_area_read(fa, 0, &h, sizeof(h)) || h.ih_magic != IMAGE_MAGIC ||
        bootutil_tlv_iter_begin(&it, &h, fa, IMAGE_TLV_ANY, false) != 0) {
        return 0;
    }
    return it.tlv_end <= fa->fa_size ? it.tlv_end : 0;
}

int umcub_inspect_verify(int image, int slot)
{
    struct flash_area fa;
    int rc = umcub_inspect_area(image, slot, &fa);
    return rc ? rc : umcub_inspect_validate(image, slot, &fa);
}

int umcub_inspect_validate(int image, int slot, const struct flash_area *area)
{
    struct flash_area fa = *area;
    struct image_header h;
    static uint8_t tmp[256];
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
#if UMCUB_CFG_ENCRYPT_IMAGES
    if (IS_ENCRYPTED(&h)) {         /* secondary: hashed while decrypting; primary: already plain */
        FIH_CALL(boot_image_validate_encrypted, fih_rc, st, &fa, &h, tmp, sizeof(tmp));
    } else
#endif
    FIH_CALL(bootutil_img_validate, fih_rc, st, &h, &fa, tmp, sizeof(tmp), NULL, 0, NULL);
#if UMCUB_CFG_BOARD_TYPE != 0 || UMCUB_CFG_IMAGE_VECTOR_CHECK
    /* Same rules as at boot: board type, reset vector in its slot. */
    if (FIH_EQ(fih_rc, FIH_SUCCESS) && !umcub_image_check(image, slot, &fa)) {
        fih_rc = FIH_FAILURE;
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
#if UMCUB_CFG_ENCRYPT_IMAGES
    /* Installed images are plain text in flash. A hash over a chosen range is
     * a readback by other means (one byte at a time, or growing prefixes):
     * outside an encrypted session only the whole stored image is hashed. */
    if ((off != 0 || len != 0) && !umcub_inspect_confidential()) {
        return UMCUB_EPERM;
    }
#endif
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
#if UMCUB_CFG_ENCRYPT_IMAGES
    /* Installed images are plain text in flash: only over a channel that is
     * itself encrypted (never USB DFU, which is not a mux request - also not
     * while a request waits for its reset, umcub_mux_flush()). */
    if (!umcub_inspect_confidential()) {
        return UMCUB_EPERM;
    }
#endif
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
