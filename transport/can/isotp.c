/*
 * ISO-TP subset, see isotp.h.
 */
#include <string.h>
#include "isotp.h"
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

    if (type == PCI_SF) {
        size_t n = d[0] & 0xFu;
        size_t hdr = 1;
        if (n == 0 && len > 2) {        /* CAN-FD escape: length in byte 1 */
            n = d[1];
            hdr = 2;
        }
        if (n == 0 || n + hdr > len || n > t->rx_cap) {
            return 0;
        }
        memcpy(t->rx_buf, d + hdr, n);
        t->rx_active = false;
        return n;
    }
    if (type == PCI_FF) {
        if (len < 2) {
            return 0;
        }
        size_t total = ((size_t)(d[0] & 0xFu) << 8) | d[1];
        size_t hdr = 2;
        if (total == 0 && len >= 6) {   /* > 4095 bytes escape */
            total = ((size_t)d[2] << 24) | ((size_t)d[3] << 16) | ((size_t)d[4] << 8) | d[5];
            hdr = 6;
        }
        if (total > t->rx_cap || total < len - hdr) {
            send_fc(t, FC_OVFL);
            t->rx_active = false;
            return 0;
        }
        t->rx_len = total;
        t->rx_off = len - hdr;
        memcpy(t->rx_buf, d + hdr, t->rx_off);
        t->rx_sn = 1;
        t->rx_active = true;
        t->rx_last = umcub_port_millis();
        send_fc(t, FC_CTS);
        return 0;
    }
    if (type == PCI_CF && t->rx_active) {
        if ((d[0] & 0xFu) != (t->rx_sn & 0xFu)) {
            t->rx_active = false;       /* lost frame: drop message */
            return 0;
        }
        size_t n = (size_t)len - 1u;
        if (n > t->rx_len - t->rx_off) {
            n = t->rx_len - t->rx_off;
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

/* Wait for a flow control frame; returns FC status or negative. */
static int wait_fc(isotp_t *t, uint8_t *bs, uint8_t *stmin)
{
    uint32_t start = umcub_port_millis();
    for (;;) {
        uint32_t id;
        uint8_t d[64], len;
        if (umcub_port_can_recv(&id, d, &len) && len >= 3 && (d[0] >> 4) == PCI_FC) {
            uint8_t st = d[0] & 0xFu;
            if (st == FC_WAIT) {
                start = umcub_port_millis();
                continue;
            }
            *bs = d[1];
            *stmin = d[2];
            return st;
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
        if (len <= 7) {
            f[0] = (uint8_t)(PCI_SF << 4 | len);
            memcpy(f + 1, msg, len);
            return send_frame(t, f, (uint8_t)(len + 1));
        }
        f[0] = PCI_SF << 4;
        f[1] = (uint8_t)len;
        memcpy(f + 2, msg, len);
        return send_frame(t, f, (uint8_t)(len + 2));
    }
    if (len > 4095) {
        return UMCUB_EINVAL;
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
    if (rc != FC_CTS) {
        return rc < 0 ? rc : UMCUB_EIO;
    }
    while (off < len) {
        size_t n = len - off > (size_t)(fl - 1u) ? (size_t)(fl - 1u) : len - off;
        f[0] = (uint8_t)((PCI_CF << 4) | (sn & 0xFu));
        memcpy(f + 1, msg + off, n);
        rc = send_frame(t, f, (uint8_t)(n + 1));
        if (rc) {
            return rc;
        }
        off += n;
        sn++;
        if (off < len) {
            if (stmin > 0 && stmin <= 127) {
                umcub_port_delay_ms(stmin);
            } else if (stmin >= 0xF1 && stmin <= 0xF9) {
                umcub_port_delay_ms(1);
            }
            if (bs && ++sent_in_block == bs) {
                sent_in_block = 0;
                rc = wait_fc(t, &bs, &stmin);
                if (rc != FC_CTS) {
                    return rc < 0 ? rc : UMCUB_EIO;
                }
            }
        }
    }
    return 0;
}
