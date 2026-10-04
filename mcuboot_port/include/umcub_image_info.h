/*
 * Reading umcub data out of MCUboot images.
 */
#ifndef UMCUB_IMAGE_INFO_H
#define UMCUB_IMAGE_INFO_H

#include <stdbool.h>
#include <stdint.h>
#include "flash_map_backend/flash_map_backend.h"

/* Board type from the protected TLV UMCUB_TLV_BOARD_TYPE of the image whose
 * header is at `hdr_off` in `fa` (0, or one sector in a swap-offset secondary
 * slot). 0 = found, 1 = image has no such TLV, < 0 = no image / read error.
 * The value is authentic only once the image itself has been validated. */
int umcub_image_board_type(const struct flash_area *fa, uint32_t hdr_off, uint32_t *type);

/* Set when MCUboot performs an upgrade or revert of image 0 in boot_go()
 * (boot_perform_update_hook): not when it refuses one (invalid image, other
 * board type, downgrade). */
extern bool umcub_image0_updated;

/* Encrypted images (UMCUB_CFG_ENCRYPT_IMAGES): after an SMP upload into the
 * primary slot, before it is decrypted in place. 0 = plain image, or an
 * encrypted one that is valid for this device (signature over the decrypted
 * payload, board type) and may be decrypted; < 0 = refuse (it stays as it is
 * and is not booted). Called from boot_serial_uploaded_hook(). */
int umcub_enc_upload_check(const struct flash_area *fap);

#endif
