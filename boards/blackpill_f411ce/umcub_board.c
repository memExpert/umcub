/*
 * Black Pill board file (compiled when present). Here: an example of a
 * board's own upload protocol (UMCUB_CFG_PROTO_USER, variants/proto_user.h),
 * host side boards/blackpill_f411ce/proto_upload.py. Not compiled otherwise.
 *
 * Frames (on a stream transport the lines "0x05 0x0E <base64> \n", one frame
 * per packet on packet transports); every request is answered with its
 * command letter and a status (0 or a negative UMCUB_E* code):
 *   'B' image:u8 size:u32le    begin an image in the primary slot
 *   'W' off:u32le data...      next bytes (off = bytes accepted so far)
 *   'E'                        finish: flush and check the image header
 *   'R'                        reset (after the answer)
 * A replacement for a protocol your devices already speak; to write an image
 * the board uses umcub_slot_* like the application does.
 */
#include "umcub_cfg.h"

#if UMCUB_CFG_PROTO_USER
#include "umcub.h"
#include "umcub_bytes.h"
#include "umcub_port.h"
#include "umcub_proto.h"

static umcub_slot_writer_t writer;
static bool active;
static uint32_t taken, total;

static void answer(const umcub_transport_t *t, uint8_t cmd, int rc)
{
    uint8_t f[6] = { cmd, (uint8_t)(int8_t)rc };
    umcub_put_le32(&f[2], taken);
    umcub_proto_reply(t, f, sizeof(f));
}

bool umcub_proto_user(const umcub_transport_t *t, const uint8_t *p, size_t n)
{
    int rc = UMCUB_EINVAL;
    if (n == 0) {
        return false;
    }
    switch (p[0]) {
    case 'B':
        if (n == 6) {
            if (active) {
                (void)umcub_slot_abort(&writer);
            }
            taken = 0;
            total = umcub_get_le32(&p[2]);
            rc = umcub_slot_begin(&writer, p[1], UMCUB_SLOT_PRIMARY, total);
            active = rc == 0;
        }
        break;
    case 'W':
        if (active && n > 5) {
            uint32_t off = umcub_get_le32(&p[1]), len = (uint32_t)n - 5;
            if (off + len == taken) {
                rc = UMCUB_OK;                  /* repeated: the answer was lost */
            } else if (off == taken && len <= total - taken) {
                rc = umcub_slot_write(&writer, &p[5], len);
                taken += rc == 0 ? len : 0;
            }
        }
        break;
    case 'E':
        if (active && taken == total) {
            active = false;
            rc = umcub_slot_finish(&writer, false, false);
        }
        break;
    case 'R':
        answer(t, 'R', UMCUB_OK);
        umcub_mux_flush(20);
        umcub_port_reset();
    default:
        return false;                           /* not ours */
    }
    answer(t, p[0], rc);
    return true;
}
#endif
