/*
 * Reading umcub data out of MCUboot images.
 */
#ifndef UMCUB_IMAGE_INFO_H
#define UMCUB_IMAGE_INFO_H

#include <stdint.h>
#include "flash_map_backend/flash_map_backend.h"

/* Board type from the protected TLV UMCUB_TLV_BOARD_TYPE of the image whose
 * header is at `hdr_off` in `fa` (0, or one sector in a swap-offset secondary
 * slot). 0 = found, 1 = image has no such TLV, < 0 = no image / read error.
 * The value is authentic only once the image itself has been validated. */
int umcub_image_board_type(const struct flash_area *fa, uint32_t hdr_off, uint32_t *type);

#endif
