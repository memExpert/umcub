/*
 * Port capability: UART (8N1, RX interrupt into a ring buffer, blocking TX).
 */
#ifndef UMCUB_PORT_UART_H
#define UMCUB_PORT_UART_H

#include <stddef.h>
#include <stdint.h>

int umcub_port_uart_init(unsigned instance, uint32_t baud, uint32_t tx_pin, uint32_t rx_pin);
void umcub_port_uart_deinit(void);
/* Non-blocking: returns number of bytes copied (0 if none). */
size_t umcub_port_uart_read(uint8_t *buf, size_t max);
/* Blocking until everything is in the transmitter. */
void umcub_port_uart_write(const uint8_t *buf, size_t len);

#endif /* UMCUB_PORT_UART_H */
