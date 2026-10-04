/*
 * MCUboot image access hooks (MCUBOOT_IMAGE_ACCESS_HOOKS), compiled when
 * UMCUB_CFG_BOARD_TYPE != 0: an image is accepted only if its signed TLV
 * UMCUB_TLV_BOARD_TYPE matches this bootloader's board type. The hook runs
 * before MCUboot validates the image; after a match MCUboot's own hash and
 * signature check follows, which also covers the TLV (protected area).
 * All other hooks keep MCUboot's regular behaviour.
 */
#include <stdbool.h>
#include <string.h>
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "umcub_log.h"
#include "umcub_image_info.h"
#include "bootutil/image.h"
#include "bootutil/bootutil_public.h"
#include "bootutil/boot_hooks.h"
#include "bootutil/fault_injection_hardening.h"
#include "sysflash/sysflash.h"

int umcub_image_board_type(const struct flash_area *fa, uint32_t hdr_off, uint32_t *type)
{
    struct image_header hdr;
    if (flash_area_read(fa, hdr_off, &hdr, sizeof(hdr)) != 0 || hdr.ih_magic != IMAGE_MAGIC) {
        return -1;
    }
    struct image_tlv_iter it;
    memset(&it, 0, sizeof(it));
#if defined(MCUBOOT_SWAP_USING_OFFSET)
    it.start_off = hdr_off;     /* TLV offsets are relative to the image start */
#else
    if (hdr_off != 0) {
        return -1;
    }
#endif
    uint32_t off;
    uint16_t len, tag;
    if (bootutil_tlv_iter_begin(&it, &hdr, fa, UMCUB_TLV_BOARD_TYPE, true) != 0) {
        return -1;
    }
    int rc = bootutil_tlv_iter_next(&it, &off, &len, &tag);
    if (rc != 0) {
        return rc > 0 ? 1 : -1;
    }
    uint8_t v[4];
    if (len != sizeof(v) || flash_area_read(fa, off, v, sizeof(v)) != 0) {
        return -1;
    }
    *type = (uint32_t)v[0] | (uint32_t)v[1] << 8 | (uint32_t)v[2] << 16 | (uint32_t)v[3] << 24;
    return 0;
}

#if UMCUB_CFG_BOARD_TYPE != 0
/* Every image header present in the slot is checked: in swap-offset mode the
 * secondary slot holds an image at offset 0 (previous image, revert copy) and/or
 * one sector further (the update) - which one MCUboot uses depends on the swap
 * state, so both must belong to this board. Headers whose TLV block cannot be
 * read (stale, half-erased copies) are left to MCUboot's own validation. */
fih_ret boot_image_check_hook(int img_index, int slot)
{
    const struct flash_area *fa;
    if (flash_area_open((uint8_t)flash_area_id_from_multi_image_slot(img_index, slot), &fa) != 0) {
        FIH_RET(FIH_FAILURE);
    }
    uint32_t offs[2] = { 0, 0 };
    unsigned n = 1;
#if defined(MCUBOOT_SWAP_USING_OFFSET)
    uint32_t start, size;
    if (slot == 1 && umcub_flash_sector_info(flash_area_get_off(fa), &start, &size) == 0) {
        offs[n++] = size;
    }
#endif
    bool bad = false;
    for (unsigned i = 0; i < n && !bad; i++) {
        uint32_t type = 0;
        int rc = umcub_image_board_type(fa, offs[i], &type);
        if (rc == 0 && type != UMCUB_CFG_BOARD_TYPE) {
            UMCUB_LOG_ERR("image %d slot %d is for board type 0x%08lx, this is 0x%08lx", img_index, slot,
                          (unsigned long)type, (unsigned long)UMCUB_CFG_BOARD_TYPE);
            bad = true;
        } else if (rc == 1) {
            UMCUB_LOG_ERR("image %d slot %d has no board type (expected 0x%08lx)", img_index, slot,
                          (unsigned long)UMCUB_CFG_BOARD_TYPE);
            bad = true;
        }
    }
    flash_area_close(fa);
    if (bad) {
        FIH_RET(FIH_FAILURE);
    }
    FIH_RET(FIH_BOOT_HOOK_REGULAR);     /* MCUboot's hash + signature validation follows */
}
#else
fih_ret boot_image_check_hook(int img_index, int slot)
{
    (void)img_index;
    (void)slot;
    FIH_RET(FIH_BOOT_HOOK_REGULAR);     /* no board type configured */
}
#endif

int boot_read_image_header_hook(int img_index, int slot, struct image_header *img_head)
{
    (void)img_index;
    (void)slot;
    (void)img_head;
    return BOOT_HOOK_REGULAR;
}

bool umcub_image0_updated;

int boot_perform_update_hook(int img_index, struct image_header *img_head, const struct flash_area *area)
{
    if (img_index == 0) {
        umcub_image0_updated = true;    /* MCUboot installs (or reverts) image 0 now */
    }
    (void)img_head;
    (void)area;
    return BOOT_HOOK_REGULAR;
}

int boot_copy_region_post_hook(int img_index, const struct flash_area *area, size_t size)
{
    (void)img_index;
    (void)area;
    (void)size;
    return 0;
}

int boot_serial_uploaded_hook(int img_index, const struct flash_area *area, size_t size)
{
    (void)size;
#if UMCUB_CFG_ENCRYPT_IMAGES
    /* Validate an encrypted upload into the primary slot before it is
     * decrypted in place; the error reaches the client in the last response. */
    if (flash_area_get_id(area) == FLASH_AREA_IMAGE_PRIMARY(img_index) && umcub_enc_upload_check(area) != 0) {
        return 3;   /* MGMT_ERR_EINVAL */
    }
#else
    (void)img_index;
    (void)area;
#endif
    return 0;
}

int boot_read_swap_state_primary_slot_hook(int image_index, struct boot_swap_state *state)
{
    (void)image_index;
    (void)state;
    return BOOT_HOOK_REGULAR;
}

int boot_reset_request_hook(bool force)
{
    (void)force;
    return 0;                               /* allow */
}
