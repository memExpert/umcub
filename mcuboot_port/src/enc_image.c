/*
 * Encrypted images (UMCUB_CFG_ENCRYPT_IMAGES) in MCUboot serial recovery.
 * Replaces boot/boot_serial/src/boot_serial_encryption.c, whose in-place
 * decryption keeps a whole flash sector on the stack (128 KiB on the H7):
 *  - boot_image_validate_encrypted(): image list of boot_serial, an encrypted
 *    image in the secondary slot is decrypted on the fly for the hash;
 *  - boot_handle_enc_fw(): after an SMP upload into the primary slot the
 *    image is decrypted there, sector by sector through one static buffer.
 * Not power-fail safe, like any upload into the primary slot: an interrupted
 * decryption leaves an invalid image, the bootloader stays in recovery and
 * the image is uploaded again. Updates through the secondary slot are
 * decrypted by MCUboot itself while it installs them.
 */
#include <string.h>
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "umcub_log.h"
#include "bootutil/bootutil.h"
#include "bootutil/bootutil_public.h"
#include "bootutil/image.h"
#include "bootutil/enc_key.h"
#include "bootutil/fault_injection_hardening.h"
#include "bootutil_priv.h"

#if UMCUB_CFG_ENCRYPT_IMAGES

static struct boot_status bs;           /* only its enckey[] is used */

/* Key of the image in `slot` into the loader state; false if it has none. */
static bool load_key(struct boot_loader_state *state, int slot, const struct image_header *hdr,
                     const struct flash_area *fap)
{
    memset(&bs, 0, sizeof(bs));
    int rc = boot_enc_load(state, slot, hdr, fap, &bs);
    if (rc == 0) {
        rc = boot_enc_set_key(BOOT_CURR_ENC_SLOT(state, slot), bs.enckey[slot]);
    }
    memset(&bs, 0, sizeof(bs));
    return rc >= 0;                     /* 1: already loaded */
}

fih_ret boot_image_validate_encrypted(struct boot_loader_state *state, const struct flash_area *fa_p,
                                      struct image_header *hdr, uint8_t *buf, uint16_t buf_size)
{
    FIH_DECLARE(fih_rc, FIH_FAILURE);
    if (MUST_DECRYPT(fa_p, BOOT_CURR_IMG(state), hdr) && !load_key(state, BOOT_SLOT_SECONDARY, hdr, fa_p)) {
        FIH_RET(fih_rc);
    }
    FIH_CALL(bootutil_img_validate, fih_rc, state, hdr, fa_p, buf, buf_size, NULL, 0, NULL);
    boot_enc_zeroize(BOOT_CURR_ENC(state));
    FIH_RET(fih_rc);
}

#if UMCUB_CFG_ENC_INPLACE
static uint8_t sector_buf[UMCUB_FAMILY_UNIFORM_SECTOR];
#endif

int boot_handle_enc_fw(const struct flash_area *fap)
{
    struct image_header hdr;
    if (flash_area_read(fap, 0, &hdr, sizeof(hdr)) != 0 || hdr.ih_magic != IMAGE_MAGIC) {
        return -1;
    }
    if (!IS_ENCRYPTED(&hdr)) {
        return 0;                       /* plain image: nothing to do */
    }
#if !UMCUB_CFG_ENC_INPLACE
    UMCUB_LOG_ERR("encrypted image in the primary slot: upload it to the secondary slot (UMCUB_CFG_ENC_INPLACE 0)");
    return -1;
#else
    struct boot_loader_state *state = boot_get_loader_state();
    boot_state_init(state);
    int rc = -1;
    if (!load_key(state, BOOT_SLOT_PRIMARY, &hdr, fap)) {
        goto out;
    }
    struct enc_key_data *enc = BOOT_CURR_ENC_SLOT(state, BOOT_SLOT_PRIMARY);

    /* Encrypted: the payload between the header and the TLVs. */
    const uint32_t start = hdr.ih_hdr_size, end = BOOT_TLV_OFF(&hdr);
    for (uint32_t off = 0; off < end;) {
        struct flash_sector fs;
        if (flash_area_get_sector(fap, (off_t)off, &fs) != 0 || fs.fs_size > sizeof(sector_buf) ||
            flash_area_read(fap, fs.fs_off, sector_buf, fs.fs_size) != 0) {
            goto out;
        }
        uint32_t lo = fs.fs_off > start ? fs.fs_off : start;
        uint32_t hi = fs.fs_off + fs.fs_size < end ? fs.fs_off + fs.fs_size : end;
        if (lo < hi) {
            uint32_t p = lo - start;    /* offset in the payload: AES-CTR counter */
            boot_enc_decrypt(enc, p, hi - lo, p & 0xFu, &sector_buf[lo - fs.fs_off]);
        }
        if (flash_area_erase(fap, fs.fs_off, fs.fs_size) != 0 ||
            flash_area_write(fap, fs.fs_off, sector_buf, fs.fs_size) != 0) {
            goto out;
        }
        umcub_port_wdg_feed();
        off = fs.fs_off + fs.fs_size;
    }
    rc = 0;
out:
    memset(sector_buf, 0, sizeof(sector_buf));
    boot_enc_zeroize(BOOT_CURR_ENC(state));
    boot_state_clear(state);
    return rc;
#endif
}

#endif /* UMCUB_CFG_ENCRYPT_IMAGES */
