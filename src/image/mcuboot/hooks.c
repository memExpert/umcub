/*
 * MCUboot image access hooks (MCUBOOT_IMAGE_ACCESS_HOOKS). Before MCUboot
 * validates an image, boot_image_check_hook() rejects
 *  - with UMCUB_CFG_IMAGE_VECTOR_CHECK, an image whose reset vector lies outside
 *    the slot it runs from (built for another image / core, or uploaded to the
 *    wrong image number), and
 *  - with UMCUB_CFG_BOARD_TYPE != 0, an image whose signed TLV
 *    UMCUB_TLV_BOARD_TYPE is missing or names another board.
 * MCUboot's own hash and signature check follows, which also covers the
 * vector table and the TLV (protected area).
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

static int board_type(const struct flash_area *fa, uint32_t hdr_off, struct image_header *hdr, uint32_t *type)
{
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
    if (bootutil_tlv_iter_begin(&it, hdr, fa, UMCUB_TLV_BOARD_TYPE, true) != 0) {
        return -1;
    }
    int rc = bootutil_tlv_iter_next(&it, &off, &len, &tag);
    if (rc != 0) {
        return rc > 0 ? 1 : -1;
    }
    /* little endian TLV value, little endian target */
    return len == sizeof(*type) && flash_area_read(fa, off, type, sizeof(*type)) == 0 ? 0 : -1;
}

int umcub_image_board_type(const struct flash_area *fa, uint32_t hdr_off, uint32_t *type)
{
    struct image_header hdr;
    return umcub_image_header(fa, hdr_off, &hdr) ? board_type(fa, hdr_off, &hdr, type) : -1;
}

#if UMCUB_CFG_IMAGE_VECTOR_CHECK
/* The reset vector (word 1 of the vector table after the header) must point
 * into the slot the image executes from: the primary slot, or the slot itself
 * with direct-xip. Encrypted content (an encrypted image read as secondary:
 * the secondary slot, or an upload in the primary slot before its in-place
 * decryption) is checked once it is plain in the primary slot. */
static bool vector_ok(int img_index, int slot, const struct flash_area *fa, uint32_t hdr_off,
                      const struct image_header *hdr)
{
    uint32_t reset;
    if (MUST_DECRYPT(fa, img_index, hdr) ||
        flash_area_read(fa, hdr_off + hdr->ih_hdr_size + 4u, &reset, sizeof(reset)) != 0) {
        return true;
    }
    const struct flash_area *run = fa;
#if !defined(MCUBOOT_DIRECT_XIP)
    if (slot != 0 && umcub_slot_area(img_index, 0, &run) != 0) {
        return false;
    }
#endif
    uint32_t start = flash_area_get_off(run) + hdr->ih_hdr_size;
    bool ok = (reset & ~1u) - start < flash_area_get_size(run) - hdr->ih_hdr_size;
    if (run != fa) {
        flash_area_close(run);
    }
    if (!ok) {
        UMCUB_LOG_ERR("image %d slot %d: reset vector 0x%08lx not in slot", img_index, slot, (unsigned long)reset);
    }
    return ok;
}
#endif

/* Every image header present in the slot is checked: in swap-offset mode the
 * secondary slot holds an image at offset 0 (previous image, revert copy) and/or
 * one sector further (the update) - which one MCUboot uses depends on the swap
 * state, so both must pass. Headers whose TLV block cannot be read (stale,
 * half-erased copies) are left to MCUboot's own validation. */
bool umcub_image_check(int img_index, int slot, const struct flash_area *fa)
{
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
        struct image_header hdr;
        if (!umcub_image_header(fa, offs[i], &hdr)) {
            continue;               /* unreadable header: left to MCUboot */
        }
#if UMCUB_CFG_IMAGE_VECTOR_CHECK
        bad = !vector_ok(img_index, slot, fa, offs[i], &hdr);
#endif
#if UMCUB_CFG_BOARD_TYPE != 0
        uint32_t type = 0;
        int rc = bad ? 0 : board_type(fa, offs[i], &hdr, &type);
        if (rc == 0 && !bad && type != UMCUB_CFG_BOARD_TYPE) {
            UMCUB_LOG_ERR("image %d slot %d is for board type 0x%08lx, this is 0x%08lx", img_index, slot,
                          (unsigned long)type, (unsigned long)UMCUB_CFG_BOARD_TYPE);
            bad = true;
        } else if (rc == 1) {
            UMCUB_LOG_ERR("image %d slot %d has no board type (expected 0x%08lx)", img_index, slot,
                          (unsigned long)UMCUB_CFG_BOARD_TYPE);
            bad = true;
        }
#endif
    }
    (void)img_index;
    return !bad;
}

fih_ret boot_image_check_hook(int img_index, int slot)
{
    const struct flash_area *fa;
    if (umcub_slot_area(img_index, slot, &fa) != 0) {
        FIH_RET(FIH_FAILURE);
    }
    bool ok = umcub_image_check(img_index, slot, fa);
    flash_area_close(fa);
    if (!ok) {
        FIH_RET(FIH_FAILURE);
    }
    FIH_RET(FIH_BOOT_HOOK_REGULAR);     /* MCUboot's hash + signature validation follows */
}

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
    (void)img_index;            /* the SMP "image" field: a slot number with direct upload */
    (void)size;
#if UMCUB_CFG_ENCRYPT_IMAGES
    /* Validate an encrypted upload into a primary slot before it is
     * decrypted in place; the error reaches the client in the last response. */
    for (int image = 0; image < UMCUB_CFG_IMAGE_NUMBER; image++) {
        if (flash_area_get_id(area) == FLASH_AREA_IMAGE_PRIMARY(image) &&
            umcub_enc_upload_check(image, area) != 0) {
            return 3;   /* MGMT_ERR_EINVAL */
        }
    }
#else
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
