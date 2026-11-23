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
#include "umcub_log.h"
#include "umcub_image_info.h"
#include "umcub_inspect.h"
#include "bootutil_priv.h"      /* boot_state_init(), boot_get_loader_state() */
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
    /* missing, malformed or unreadable once the TLV area is found: refused */
    if (bootutil_tlv_iter_next(&it, &off, &len, &tag) != 0 || len != sizeof(*type)) {
        return 1;
    }
    /* little endian TLV value, little endian target */
    return flash_area_read(fa, off, type, sizeof(*type)) == 0 ? 0 : 1;
}

int umcub_image_board_type(const struct flash_area *fa, uint32_t hdr_off, uint32_t *type)
{
    struct image_header hdr;
    return umcub_image_header(fa, hdr_off, &hdr) ? board_type(fa, hdr_off, &hdr, type) : -1;
}

#if UMCUB_CFG_IMAGE_VECTOR_CHECK
/* The vector table after the header: the reset vector (word 1) is a Thumb
 * address inside the signed body as it lies in the slot the image executes
 * from (the primary slot, or the slot itself with direct-xip), the table
 * starts on the VTOR alignment of the family (the jump sets VTOR there), and
 * the initial SP (word 0) is word aligned in the SRAM region of the Cortex-M
 * memory map (0x10000000.. covers CCM SRAM aliases). Encrypted content (an
 * encrypted image read as secondary: the secondary slot, or an upload in the
 * primary slot before its in-place decryption) is checked once it is plain
 * in the primary slot. */
static bool vector_ok(int img_index, int slot, const struct flash_area *fa, uint32_t hdr_off,
                      const struct image_header *hdr)
{
    const struct flash_area *run = fa;
#if !defined(MCUBOOT_DIRECT_XIP)
    if (slot != 0 && umcub_slot_area(img_index, 0, &run) != 0) {
        return false;
    }
#endif
    uint32_t start = flash_area_get_off(run) + hdr->ih_hdr_size;
    if (run != fa) {
        flash_area_close(run);
    }
    /* The alignment comes from the plain header: also for an encrypted update, before it is installed. */
    uint32_t vt[2] = { 0, 0 };
    bool ok = start % UMCUB_FAMILY_VTOR_ALIGN == 0;
    if (ok && !MUST_DECRYPT(fa, img_index, hdr)) {
        (void)flash_area_read(fa, hdr_off + hdr->ih_hdr_size, vt, sizeof(vt));  /* a read error: umcub_image_check */
        const uint32_t sp = vt[0], reset = vt[1];
        ok = (reset & 1u) && (reset & ~1u) - start < hdr->ih_img_size && (sp & 3u) == 0 &&
             sp - 0x10000001u < 0x30000000u;
    }
    if (!ok) {
        UMCUB_LOG_ERR("image %d slot %d: vector table at 0x%08lx SP 0x%08lx reset 0x%08lx not for this slot",
                      img_index, slot, (unsigned long)start, (unsigned long)vt[0], (unsigned long)vt[1]);
    }
    return ok;
}
#endif

/* Every image header present in the slot is checked: in swap-offset mode the
 * secondary slot holds an image at offset 0 (previous image, revert copy) and/or
 * one sector further (the update) - which one MCUboot uses depends on the swap
 * state, so both must pass. Headers whose TLV area is not found (stale,
 * half-erased copies) are left to MCUboot's own validation; a flash read
 * error anywhere in the check refuses the slot (a transient one must not
 * skip it while MCUboot's later reads succeed). */
bool umcub_image_check(int img_index, int slot, const struct flash_area *fa)
{
    uint32_t off = 0;
#if defined(MCUBOOT_SWAP_USING_OFFSET)
    /* The header MCUboot validates: the update one sector in, the revert copy at 0 - never a half-copied one
     * a swap cut by a power loss left at the other place (MCUboot's area only; 0 for an area of the caller). */
    off = boot_get_state_secondary_offset(boot_get_loader_state(), fa);
#endif
    const unsigned read_errors = umcub_flash_read_errors;
    bool bad = false;
    struct image_header hdr;
    if (umcub_image_header(fa, off, &hdr)) {    /* else no image header (a read error: the count below) */
#if UMCUB_CFG_IMAGE_VECTOR_CHECK
        bad = !vector_ok(img_index, slot, fa, off, &hdr);
#endif
#if UMCUB_CFG_BOARD_TYPE != 0
        uint32_t type = 0;
        int rc = bad ? -1 : board_type(fa, off, &hdr, &type);
        if (rc == 0 && type != UMCUB_CFG_BOARD_TYPE) {
            UMCUB_LOG_ERR("image %d slot %d is for board type 0x%08lx, this is 0x%08lx", img_index, slot,
                          (unsigned long)type, (unsigned long)UMCUB_CFG_BOARD_TYPE);
            bad = true;
        } else if (rc == 1) {
            UMCUB_LOG_ERR("image %d slot %d has no valid board type (expected 0x%08lx)", img_index, slot,
                          (unsigned long)UMCUB_CFG_BOARD_TYPE);
            bad = true;
        }
#endif
    }
    (void)img_index;
    /* any failed read: a header, a TLV area MCUboot's iterator gave up on, the vectors, the board type */
    return !bad && umcub_flash_read_errors == read_errors;
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
    (void)img_index;
    (void)img_head;
    (void)area;
    return BOOT_HOOK_REGULAR;
}

#if UMCUB_CFG_IMAGE_NUMBER > 1 && !defined(MCUBOOT_DIRECT_XIP)
/* (Not in direct-xip(-revert): there a second loader pass would scramble slots the first pass just marked.) */
fih_ret umcub_boot_go(struct boot_rsp *rsp, bool *all)
{
    FIH_DECLARE(fih_rc, FIH_FAILURE);
    *all = true;
    FIH_CALL(boot_go, fih_rc, rsp);
    if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
        UMCUB_LOG_WRN("boot_go failed, trying image 0 alone");
        *all = false;
        static struct image_header hdr0;
        const struct flash_area *fa;
        if (umcub_image0_updated && umcub_inspect_verify(0, 0) == 0 && umcub_slot_area(0, 0, &fa) == 0) {
            bool ok = umcub_image_header(fa, 0, &hdr0);
            rsp->br_flash_dev_id = flash_area_get_device_id(fa);
            rsp->br_image_off = flash_area_get_off(fa);
            rsp->br_hdr = &hdr0;
            flash_area_close(fa);
            if (ok) {
                FIH_RET(FIH_SUCCESS);
            }
        }
        boot_state_init(boot_get_loader_state());
        FIH_CALL(boot_go_for_image_id, fih_rc, rsp, 0);
    }
    FIH_RET(fih_rc);
}
#endif

int boot_copy_region_post_hook(int img_index, const struct flash_area *area, size_t size)
{
    if (img_index == 0) {
        umcub_image0_updated = true;    /* installed or reverted, also a swap an earlier boot began */
    }
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
