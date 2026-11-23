/*
 * ISO-TP subset, see isotp.h.
 */
#include <string.h>
#include "umcub_cfg.h"
#include "isotp.h"
#include "umcub_bytes.h"
#include "umcub_port.h"
#include "umcub_port_can.h"

#define PCI_SF  0x0u
#define PCI_FF  0x1u
#define PCI_CF  0x2u
#define PCI_FC  0x3u
#define FC_CTS  0u
#define FC_WAIT 1u
#define FC_OVFL 2u
#define TIMEOUT_MS 1000u
#define WFT_MAX    16u      /* FC WAIT frames per flow control (ISO 15765-2 N_WFTmax) */

static int send_frame(isotp_t *t, const uint8_t *data, uint8_t len)
{
    uint32_t start = umcub_port_millis();
    int rc;
    while ((rc = umcub_port_can_send(t->tx_id, data, len)) == UMCUB_EBUSY) {
        if ((uint32_t)(umcub_port_millis() - start) > TIMEOUT_MS) {
            return UMCUB_ETIMEOUT;
        }
    }
    return rc;
}

static void send_fc(isotp_t *t, uint8_t status)
{
    uint8_t fc[3] = { (uint8_t)((PCI_FC << 4) | status), 0 /* BS */, 0 /* STmin */ };
    (void)send_frame(t, fc, sizeof(fc));
}

size_t isotp_on_frame(isotp_t *t, const uint8_t *d, uint8_t len)
{
    if (len < 1) {
        return 0;
    }
    uint8_t type = d[0] >> 4;

    if (t->rx_active && (uint32_t)(umcub_port_millis() - t->rx_last) > TIMEOUT_MS) {
        t->rx_active = false;
    }

    /* ISO 15765-2 has the receiver ignore malformed SF / FF: here, before the
     * reception in progress is touched. */
    if (type == PCI_SF) {
        size_t n = d[0] & 0xFu;
        size_t hdr = 1;
        if (UMCUB_CFG_CAN_FD && len > 8) {      /* CAN-FD: the length in byte 1 (escape), and only there */
            if (n != 0) {
                return 0;
            }
            n = d[1];
            hdr = 2;
            /* more than the next smaller CAN-FD frame holds (ISO 15765-2 table 13: at least 8 bytes in 12, 11 in
             * 16, 15 in 20, 19 in 24, 23 in 32, 31 in 48, 47 in 64); len is one of these sizes (the DLC) */
            if (n + (len <= 12 ? 4u : len <= 24 ? 5u : len <= 32 ? 9u : 17u) < len) {
                return 0;
            }
        }
        if (n == 0 || n + hdr > len || n > t->rx_cap) {
            return 0;
        }
        memcpy(t->rx_buf, d + hdr, n);
        t->rx_active = false;
        return n;
    }
    if (type == PCI_FF) {
        if (len < 8) {                  /* a first frame fills its CAN frame */
            return 0;
        }
        size_t total = ((size_t)(d[0] & 0xFu) << 8) | d[1];
        size_t hdr = 2;
        if (total == 0) {               /* > 4095 bytes escape */
            total = umcub_get_be32(d + 2);
            hdr = 6;
            if (total <= 0xFFFu) {
                return 0;
            }
        }
        if (total < (len > 8 ? len - 1u : 8u)) {
            return 0;                   /* fits a single frame */
        }
        if (total > t->rx_cap) {
            send_fc(t, FC_OVFL);
            t->rx_active = false;
            return 0;
        }
        t->rx_len = total;
        t->rx_dl = len;
        t->rx_off = len - hdr;
        memcpy(t->rx_buf, d + hdr, t->rx_off);
        t->rx_sn = 1;
        t->rx_active = true;
        t->rx_last = umcub_port_millis();
        send_fc(t, FC_CTS);
        return 0;
    }
    if (type == PCI_CF && t->rx_active) {
        size_t n = (size_t)len - 1u, left = t->rx_len - t->rx_off;
        if (len > t->rx_dl || (len != t->rx_dl && n < left)) {
            return 0;                   /* the first frame's size; only the last may be shorter */
        }
        if ((d[0] & 0xFu) != (t->rx_sn & 0xFu)) {
            t->rx_active = false;       /* lost frame: drop message */
            return 0;
        }
        if (n > left) {
            n = left;
        }
        memcpy(t->rx_buf + t->rx_off, d + 1, n);
        t->rx_off += n;
        t->rx_sn++;
        t->rx_last = umcub_port_millis();
        if (t->rx_off == t->rx_len) {
            t->rx_active = false;
            return t->rx_len;
        }
    }
    return 0;
}

/* Wait for a flow control frame: 0 = clear to send, else an error. */
static int wait_fc(isotp_t *t, uint8_t *bs, uint8_t *stmin)
{
    uint32_t start = umcub_port_millis();
    unsigned waits = 0;
    for (;;) {
        uint32_t id;
        uint8_t d[64], len;
        if (umcub_port_can_recv(&id, d, &len) && len >= 3 && (d[0] >> 4) == PCI_FC) {
            uint8_t st = d[0] & 0xFu;
            if (st == FC_WAIT) {
                if (++waits > WFT_MAX) {
                    return UMCUB_ETIMEOUT;
                }
                start = umcub_port_millis();
                continue;
            }
            *bs = d[1];
            *stmin = d[2];
            return st == FC_CTS ? 0 : UMCUB_EIO;
        }
        if ((uint32_t)(umcub_port_millis() - start) > TIMEOUT_MS) {
            return UMCUB_ETIMEOUT;
        }
        umcub_port_wdg_feed();
    }
}

int isotp_send(isotp_t *t, const uint8_t *msg, size_t len)
{
    uint8_t f[64];
    uint8_t fl = t->frame_len;
    bool fd = fl > 8;

    if (len <= (size_t)(fl - (fd ? 2u : 1u)) && (len <= 7 || fd)) {
        size_t hdr = len <= 7 ? 1u : 2u;    /* CAN-FD SF > 7 bytes: length in byte 1 */
        f[0] = (uint8_t)(PCI_SF << 4 | (hdr == 1 ? len : 0));
        f[1] = (uint8_t)len;
        memcpy(f + hdr, msg, len);
        return send_frame(t, f, (uint8_t)(len + hdr));
    }
    if (len > 0xFFFu) {
        return UMCUB_EINVAL;            /* the first frame's 12-bit length (a board protocol's answer can be longer) */
    }
    f[0] = (uint8_t)((PCI_FF << 4) | (len >> 8));
    f[1] = (uint8_t)len;
    size_t off = fl - 2u;
    memcpy(f + 2, msg, off);
    int rc = send_frame(t, f, fl);
    if (rc) {
        return rc;
    }
    uint8_t sn = 1, bs = 0, stmin = 0, sent_in_block = 0;
    rc = wait_fc(t, &bs, &stmin);
    if (rc) {
        return rc;
    }
    while (off < len) {
        size_t n = len - off > (size_t)(fl - 1u) ? (size_t)(fl - 1u) : len - off;
        f[0] = (uint8_t)((PCI_CF << 4) | (sn & 0xFu));
        memcpy(f + 1, msg + off, n);
        rc = send_frame(t, f, (uint8_t)(n + 1));
        if (rc) {
            return rc;
        }
        umcub_port_wdg_feed();          /* progress: STmin can stretch a long answer over seconds */
        off += n;
        sn++;
        if (off < len) {
            /* STmin: 0-127 ms, 0xF1-0xF9 100-900 us; reserved values mean the
             * longest, 127 ms (ISO 15765-2). A minimum from the end of the
             * previous frame, so wait until it has left first. */
            if (stmin) {
                uint32_t start = umcub_port_millis();
                while (!umcub_port_can_tx_idle()) {
                    if ((uint32_t)(umcub_port_millis() - start) > TIMEOUT_MS) {
                        return UMCUB_ETIMEOUT;
                    }
                }
                umcub_port_delay_us(stmin <= 127 ? stmin * 1000u :
                                    stmin >= 0xF1 && stmin <= 0xF9 ? (stmin - 0xF0u) * 100u : 127000u);
            }
            if (bs && ++sent_in_block == bs) {
                sent_in_block = 0;
                rc = wait_fc(t, &bs, &stmin);
                if (rc) {
                    return rc;
                }
            }
        }
    }
    return 0;
}
