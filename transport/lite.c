/*
 * Lite upload protocol (UMCUB_CFG_LITE_UPLOAD): images without SMP, written
 * with the slot writer (umcub_slot_*). Frames: transport/include/umcub_lite.h.
 */
#include <string.h>
#include "umcub_cfg.h"

#if UMCUB_CFG_LITE_UPLOAD
#include "umcub_lite.h"
#include "umcub.h"
#include "umcub_log.h"
#include "umcub_port.h"
#include "crc/crc16.h"
#if UMCUB_CFG_ENCRYPT_IMAGES
#include "flash_map_backend/flash_map_backend.h"
#include "sysflash/sysflash.h"
#include "boot_serial/boot_serial_encryption.h"
#include "umcub_image_info.h"
#endif

static umcub_slot_writer_t writer;
static bool active;
static uint32_t taken;      /* image bytes accepted (written or buffered) */
static uint32_t total;

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void answer(const umcub_transport_t *t, int rc)
{
    uint8_t f[9] = { UMCUB_LITE_MAGIC, 'k', (uint8_t)(int8_t)rc, (uint8_t)taken, (uint8_t)(taken >> 8),
                     (uint8_t)(taken >> 16), (uint8_t)(taken >> 24) };
    uint16_t crc = crc16_ccitt(0, f, 7);
    f[7] = (uint8_t)(crc >> 8);
    f[8] = (uint8_t)crc;
    umcub_mux_send_frame(t, UMCUB_FRAME_LITE, f, sizeof(f));
}

static int begin(const uint8_t *p, size_t n)
{
    if (n != 6) {
        return UMCUB_EINVAL;
    }
    if (active) {
        (void)umcub_slot_abort(&writer);    /* a new upload replaces an unfinished one */
        active = false;
    }
    taken = 0;
    total = get32(&p[2]);
    int rc = umcub_slot_begin(&writer, p[0], p[1] ? UMCUB_SLOT_SECONDARY : UMCUB_SLOT_PRIMARY, total);
    active = rc == 0;
    UMCUB_LOG_INF("lite: upload image %u slot %u, %lu bytes: rc %d", p[0], p[1], (unsigned long)total, rc);
    return rc;
}

static int data(const uint8_t *p, size_t n)
{
    if (!active || n < 4) {
        return UMCUB_EINVAL;
    }
    uint32_t off = get32(p);
    size_t len = n - 4;
    if (off + len == taken) {
        return UMCUB_OK;                    /* repeated frame (answer was lost) */
    }
    if (off != taken || len > total - taken) {
        return UMCUB_EINVAL;
    }
    int rc = umcub_slot_write(&writer, &p[4], len);
    if (rc == 0) {
        taken += (uint32_t)len;
    }
    return rc;
}

static int end(const uint8_t *p, size_t n)
{
    if (!active || n != 1 || taken != total) {
        return UMCUB_EINVAL;
    }
    active = false;
    int slot = writer.slot;
    int rc = umcub_slot_finish(&writer, slot == 1 && (p[0] & 1u), (p[0] & 2u) != 0);
#if UMCUB_CFG_ENCRYPT_IMAGES
    if (rc == 0 && slot == 0) {             /* like an SMP upload: validate, decrypt in place */
        const struct flash_area *fa;
        if (flash_area_open((uint8_t)flash_area_id_from_multi_image_slot(writer.image, 0), &fa) == 0) {
            if (umcub_enc_upload_check(fa) != 0 || boot_handle_enc_fw(fa) != 0) {
                rc = UMCUB_EIO;
            }
            flash_area_close(fa);
        }
    }
#endif
    UMCUB_LOG_INF("lite: upload done: rc %d", rc);
    return rc;
}

bool umcub_lite_rx(const umcub_transport_t *t, const uint8_t *f, size_t len)
{
    if (len < 1 || f[0] != UMCUB_LITE_MAGIC) {
        return false;
    }
    if (len < 4 || crc16_ccitt(0, f, (int)len - 2) != (uint16_t)(f[len - 2] << 8 | f[len - 1])) {
        answer(t, UMCUB_EINVAL);            /* damaged: the host repeats it */
        return true;
    }
    const uint8_t *p = &f[2];
    size_t n = len - 4;
    switch (f[1]) {
    case 'B':
        answer(t, begin(p, n));
        break;
    case 'D':
        answer(t, data(p, n));
        break;
    case 'E':
        answer(t, end(p, n));
        break;
    case 'R':
        answer(t, UMCUB_OK);
        for (uint32_t start = umcub_port_millis(); (uint32_t)(umcub_port_millis() - start) < 50u;) {
            umcub_transports_poll();        /* let the answer leave (USB, Ethernet) */
        }
        umcub_port_reset();
        break;
    default:
        answer(t, UMCUB_ENOTSUP);
        break;
    }
    return true;
}
#endif /* UMCUB_CFG_LITE_UPLOAD */
