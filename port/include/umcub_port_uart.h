/*
 * Port capability: UART (8N1, RX interrupt into a ring buffer, blocking TX),
 * optionally half-duplex RS485 with a driver-enable (DE) pin.
 */
#ifndef UMCUB_PORT_UART_H
#define UMCUB_PORT_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    unsigned instance;
    uint32_t baud;
    uint32_t tx_pin;
    uint32_t rx_pin;
    /* RS485 driver enable, UMCUB_PIN_NONE = full duplex. Asserted while the
     * port transmits; the receiver is off meanwhile (no echo of own bytes).
     * A pin with the USART's RTS/DE alternate function uses the hardware DE
     * of the USART where the family has one, any other pin is driven by
     * software around each write. */
    uint32_t de_pin;
    bool de_active_high;
} umcub_uart_cfg_t;

int umcub_port_uart_init(const umcub_uart_cfg_t *cfg);
void umcub_port_uart_deinit(void);
/* Non-blocking: returns number of bytes copied (0 if none). */
size_t umcub_port_uart_read(uint8_t *buf, size_t max);
/* Blocking until everything is sent (with DE: until the last stop bit). */
void umcub_port_uart_write(const uint8_t *buf, size_t len);

#endif /* UMCUB_PORT_UART_H */
