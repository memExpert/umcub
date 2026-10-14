/*
 * Checking what is in the image slots from the host side:
 *  - verify: full MCUboot validation (SHA-256 over flash + signature)
 *  - hash:   SHA-256 of a range of a slot, to compare with what was sent
 *  - read:   raw readback (only with UMCUB_CFG_READBACK)
 * Addressing is always (image, slot, offset): the bootloader region and
 * anything outside the image slots cannot be reached.
 */
#ifndef UMCUB_INSPECT_H
#define UMCUB_INSPECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "flash_map_backend/flash_map_backend.h"

/* Resolve image/slot to the area that holds the image (swap-offset: the
 * update image in the secondary slot starts one sector in). */
int umcub_inspect_area(int image, int slot, struct flash_area *out);

/* Bytes of the stored image: header + payload + TLVs (0 if no image). */
uint32_t umcub_inspect_image_len(const struct flash_area *fa);

/* 0 valid; UMCUB_ENOTSUP no image; UMCUB_EIO invalid hash/signature. */
int umcub_inspect_verify(int image, int slot);

/* Same check for the image at the start of `fa` (image `image`, `slot` for
 * the board-type rule). An area whose id is the secondary slot's is hashed
 * while decrypting (encrypted images); the primary is taken as plain. */
int umcub_inspect_validate(int image, int slot, const struct flash_area *fa);

/* SHA-256 of [off, off + len) of the slot; len 0 = whole stored image. */
int umcub_inspect_hash(int image, int slot, uint32_t off, uint32_t len, uint8_t out[32], uint32_t *hashed_len);

/* Implemented by the protocol layer (proto/mux.c): true while the request
 * being handled came over an encrypted umcub link session (SECURE +
 * UMCUB_CFG_LINK_ENCRYPT). With UMCUB_CFG_ENCRYPT_IMAGES, readback and
 * partial hashes of the (plain text) installed images need it. */
bool umcub_inspect_confidential(void);

/* Raw read (UMCUB_EINVAL unless UMCUB_CFG_READBACK). */
int umcub_inspect_read(int image, int slot, uint32_t off, void *buf, uint32_t len);

#endif
