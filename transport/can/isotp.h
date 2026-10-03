/*
 * ISO 15765-2 (ISO-TP) subset for carrying SMP packets over CAN / CAN-FD:
 * single/first/consecutive/flow-control frames, BS = 0 / STmin = 0 on
 * receive, BS / STmin honoured on transmit, normal addressing.
 */
#ifndef UMCUB_ISOTP_H
#define UMCUB_ISOTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t frame_len;          /* 8 (classic) or 64 (FD) */
    uint32_t tx_id;
    /* receive state */
    uint8_t *rx_buf;
    size_t rx_cap;
    size_t rx_len;              /* expected total */
    size_t rx_off;
    uint8_t rx_sn;
    bool rx_active;
    uint32_t rx_last;
} isotp_t;

/* Feed one received frame. Returns the length of a completed message in
 * rx_buf (> 0), 0 otherwise. */
size_t isotp_on_frame(isotp_t *t, const uint8_t *data, uint8_t len);

/* Send a whole message (blocking, handles flow control). */
int isotp_send(isotp_t *t, const uint8_t *msg, size_t len);

#endif
