/*
 * umcub application library.
 *
 *   #include "umcub.h"
 *
 *   const umcub_handoff_t *bi = umcub_boot_info();      // what the bootloader did
 *   umcub_version_t v; umcub_boot_version(&v);          // bootloader version
 *   umcub_confirm();                                     // keep this image (swap/revert modes)
 *   umcub_enter_bootloader(0);                           // reboot into recovery/update mode
 *
 *   // Write a new image yourself (any transport the application owns):
 *   umcub_slot_writer_t w;
 *   umcub_slot_begin(&w, 0, UMCUB_SLOT_DEFAULT, image_size);
 *   umcub_slot_write(&w, chunk, len);   // repeat
 *   umcub_slot_finish(&w, true, false); // verify header, mark for test boot
 *
 * All functions return 0 / positive on success and a negative UMCUB_E* code
 * on failure (see umcub_port.h).
 */
#ifndef UMCUB_H
#define UMCUB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "umcub_handoff.h"
#include "umcub_config_types.h"   /* UMCUB_SLOT_*, UMCUB_MODE_* */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- information -------------------------------------------------------- */

/* Handoff data written by the bootloader before starting this application,
 * or NULL if absent/corrupted (e.g. the application was started by a
 * debugger without the bootloader). */
const umcub_handoff_t *umcub_boot_info(void);

/* Bootloader's static info block (always available when a umcub bootloader
 * is installed), or NULL. */
const umcub_info_block_t *umcub_boot_info_block(void);

/* Bootloader version: from the info block, falling back to the handoff. */
int umcub_boot_version(umcub_version_t *v);

/* Version in the image header of an image slot (slot 0 primary, 1 secondary). */
int umcub_image_version(int image, int slot, umcub_version_t *v);

/* ---- control ------------------------------------------------------------ */

/* Ask the bootloader to stay in recovery/update mode and reset. `arg` is a
 * free value printed by the bootloader. Never returns. */
__attribute__((noreturn)) void umcub_enter_bootloader(uint32_t arg);

/* Node address of this device on a shared bus (RS485, CAN, ...), used by the
 * bootloader for addressing (umcub link, CAN IDs). Call it on every start:
 * the value lives in the handoff RAM and survives resets, not power cycles.
 * 0 = unassigned. umcub_boot_info()->node_addr shows what the bootloader used. */
void umcub_set_node_address(uint16_t addr);

/* Mark the running image as good (swap and direct-xip-revert modes:
 * otherwise the bootloader reverts it on the next reset). */
int umcub_confirm(void);
int umcub_confirm_image(int image);
/* 1 confirmed, 0 not confirmed (test boot), negative on error. */
int umcub_is_confirmed(int image);

/* Mark the new image for the next boot: swap/overwrite - the secondary slot;
 * direct-xip(-revert) - the slot this code is not running from.
 * permanent = false: test boot (reverted unless confirmed). */
int umcub_request_upgrade(int image, bool permanent);

/* ---- writing an image from the application ------------------------------- */

#define UMCUB_SLOT_DEFAULT  (-1)   /* UMCUB_CFG_APP_WRITE_SLOT */

typedef struct {
    uint32_t base;          /* slot start address */
    uint32_t size;          /* slot size */
    uint32_t off;           /* bytes accepted so far */
    uint32_t erased_end;    /* [base, base + erased_end) erased */
    uint32_t last_sector;   /* offset of the trailer sector (pre-erased) */
    int image;
    int slot;               /* resolved: 0 primary, 1 secondary */
    uint8_t buf_len;
    uint8_t buf[32];        /* >= flash program unit */
} umcub_slot_writer_t;

/* Prepare `slot` of `image` (UMCUB_SLOT_PRIMARY/SECONDARY/INACTIVE or
 * UMCUB_SLOT_DEFAULT) for an image of `total_size` bytes. Refuses
 * (UMCUB_EBUSY) a slot code is running from: the application itself, or any
 * image the bootloader started (the other core's image on dual-core parts). */
int umcub_slot_begin(umcub_slot_writer_t *w, int image, int slot, uint32_t total_size);
int umcub_slot_write(umcub_slot_writer_t *w, const void *data, size_t len);
/* Flush, check the MCUboot image header and optionally mark the written
 * slot for the next boot. In swap modes umcub_slot_begin() refuses the
 * secondary slot (UMCUB_EBUSY) while the running image is still on test:
 * the secondary then holds the previous image needed for a revert. */
int umcub_slot_finish(umcub_slot_writer_t *w, bool request_upgrade, bool permanent);
/* Abort: erase what was written. */
int umcub_slot_abort(umcub_slot_writer_t *w);

/* ---- hooks the application may override (weak defaults provided) -------- */

/* Millisecond time base used for flash timeouts (default: DWT cycle counter). */
uint32_t umcub_port_millis(void);
/* Called during long flash operations - feed your watchdog here. */
void umcub_port_wdg_feed(void);

#ifdef __cplusplus
}
#endif

#endif /* UMCUB_H */
