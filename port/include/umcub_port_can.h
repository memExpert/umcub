/*
 * Port capability: CAN / CAN-FD controller (polled, one RX filter).
 */
#ifndef UMCUB_PORT_CAN_H
#define UMCUB_PORT_CAN_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    unsigned instance;
    uint32_t bitrate;
    uint32_t data_bitrate;   /* CAN-FD data phase (fd = true) */
    bool fd;
    bool loopback;           /* internal loopback, no bus needed */
    uint32_t rx_id;          /* only this id is received */
    bool ext;                /* 29-bit ids */
    uint32_t tx_pin;
    uint32_t rx_pin;
} umcub_can_cfg_t;

int umcub_port_can_init(const umcub_can_cfg_t *cfg);
void umcub_port_can_deinit(void);
/* len <= 8 (classic) or <= 64 (FD; padded to the next valid length).
 * Returns UMCUB_EBUSY if the TX FIFO is full. */
int umcub_port_can_send(uint32_t id, const uint8_t *data, uint8_t len);
bool umcub_port_can_recv(uint32_t *id, uint8_t *data, uint8_t *len);

#endif
