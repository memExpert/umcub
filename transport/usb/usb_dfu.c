/*
 * USB DFU 1.1 download into an image's secondary slot (tinyUSB DFU class).
 * Alt setting n = image n. After manifestation the image is marked for a
 * test boot (MCUboot swap) or simply installed (overwrite); on DFU detach
 * (dfu-util -R) the device resets into the new image.
 *
 *   dfu-util -a 0 -D app.signed.bin -R
 */
#include "tusb.h"
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "umcub_log.h"
#include "umcub.h"
#include "umcub_handoff.h"

void umcub_handoff_note_transport(uint8_t id);

static umcub_slot_writer_t writer;
static bool active;
static bool reset_pending;
static uint32_t reset_at;

uint32_t tud_dfu_get_timeout_cb(uint8_t alt, uint8_t state)
{
    (void)alt;
    if (state != DFU_DNBUSY) {
        return 0;
    }
    /* The host waits this long before polling again. Only a block that
     * reaches a not yet erased sector (and the first block: slot
     * preparation) pays for a sector erase (H7: typ. 2 s, max 4 s). */
    uint32_t next = writer.off + writer.buf_len + CFG_TUD_DFU_XFER_BUFSIZE;
    bool erase = !active || (next > writer.erased_end && writer.erased_end < writer.last_sector);
    return erase ? 4500 : 5;
}

void tud_dfu_download_cb(uint8_t alt, uint16_t block_num, uint8_t const *data, uint16_t length)
{
    if (block_num == 0) {
        if (active) {
            umcub_slot_abort(&writer);
        }
        active = umcub_slot_begin(&writer, alt, UMCUB_SLOT_SECONDARY, 0) == 0;
        umcub_handoff_note_transport(UMCUB_TRANSPORT_USB_DFU);
        UMCUB_LOG_INF("dfu: download to image %u %s", alt, active ? "started" : "refused");
    }
    if (!active || writer.off + writer.buf_len != (uint32_t)block_num * CFG_TUD_DFU_XFER_BUFSIZE ||
        umcub_slot_write(&writer, data, length) != 0) {
        active = false;
        tud_dfu_finish_flashing(DFU_STATUS_ERR_WRITE);
        return;
    }
    tud_dfu_finish_flashing(DFU_STATUS_OK);
}

void tud_dfu_manifest_cb(uint8_t alt)
{
    (void)alt;
    int rc = active ? umcub_slot_finish(&writer, true, false) : UMCUB_EINVAL;
    active = false;
    UMCUB_LOG_INF("dfu: manifest %s", rc == 0 ? "ok, image pending" : "failed");
    tud_dfu_finish_flashing(rc == 0 ? DFU_STATUS_OK : DFU_STATUS_ERR_VERIFY);
}

#if UMCUB_CFG_READBACK
#include "umcub_inspect.h"
/* dfu-util -a <image> -U file.bin: read back the image in the secondary
 * slot (where DFU downloads go), up to the end of the stored image. */
uint16_t tud_dfu_upload_cb(uint8_t alt, uint16_t block_num, uint8_t *data, uint16_t length)
{
    struct flash_area fa;
    if (umcub_inspect_area(alt, 1, &fa)) {
        return 0;
    }
    uint32_t total = umcub_inspect_image_len(&fa);
    uint32_t off = (uint32_t)block_num * CFG_TUD_DFU_XFER_BUFSIZE;
    if (off >= total) {
        return 0;               /* short (empty) block ends the upload */
    }
    uint32_t n = total - off < length ? total - off : length;
    return umcub_inspect_read(alt, 1, off, data, n) == 0 ? (uint16_t)n : 0;
}
#else
uint16_t tud_dfu_upload_cb(uint8_t alt, uint16_t block_num, uint8_t *data, uint16_t length)
{
    (void)alt;
    (void)block_num;
    (void)data;
    (void)length;
    return 0;   /* readback disabled (UMCUB_CFG_READBACK) */
}
#endif

void tud_dfu_abort_cb(uint8_t alt)
{
    (void)alt;
    if (active) {
        umcub_slot_abort(&writer);
        active = false;
    }
}

void tud_dfu_detach_cb(void)
{
    reset_pending = true;
    reset_at = umcub_port_millis();
}

void umcub_usb_dfu_poll(void)
{
    /* Let the status stage of DFU_DETACH complete before resetting. */
    if (reset_pending && (uint32_t)(umcub_port_millis() - reset_at) > 50u) {
        UMCUB_LOG_INF("dfu: detach -> reset");
        umcub_port_reset();
    }
}
