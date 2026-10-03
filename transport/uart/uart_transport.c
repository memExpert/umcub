/*
 * UART transport: SMP serial (NLIP framing) + log output.
 */
#include "umcub_cfg.h"
#include "umcub_log.h"
#include "umcub_port.h"
#include "umcub_port_uart.h"
#include "umcub_transport.h"
#include "umcub_handoff.h"

static void uart_write(const uint8_t *buf, size_t len);

static void log_sink(const char *s, size_t len)
{
    uart_write((const uint8_t *)s, len);
}

static bool up;
static uint32_t last_rx;    /* millis of the last received byte (RS485 turnaround) */

static size_t uart_read(uint8_t *buf, size_t max)
{
    size_t n = umcub_port_uart_read(buf, max);
    if (n) {
        last_rx = umcub_port_millis();
    }
    return n;
}

static void uart_write(const uint8_t *buf, size_t len)
{
#if UMCUB_CFG_UART_TURNAROUND_MS > 0
    /* Half duplex: leave the host time to switch its transceiver to receive. */
    while ((uint32_t)(umcub_port_millis() - last_rx) < UMCUB_CFG_UART_TURNAROUND_MS) {
        umcub_port_idle();
    }
#endif
    umcub_port_uart_write(buf, len);
}

static int uart_init(void)
{
    if (up) {
        return 0;
    }
    const umcub_uart_cfg_t c = {
        .instance = UMCUB_CFG_UART_INSTANCE,
        .baud = UMCUB_CFG_UART_BAUD,
        .tx_pin = UMCUB_CFG_UART_TX_PIN,
        .rx_pin = UMCUB_CFG_UART_RX_PIN,
        .de_pin = UMCUB_CFG_UART_DE_PIN,
        .de_active_high = UMCUB_CFG_UART_DE_ACTIVE,
    };
    int rc = umcub_port_uart_init(&c);
    if (rc == 0) {
        up = true;
        /* Log text only on a plain UART: on a link transport it would be clear
         * text next to the frames (and share an RS485 bus). */
        if (UMCUB_CFG_UART_LINK == UMCUB_LINK_PLAIN) {
            umcub_log_set_sink(log_sink);
        }
    }
    return rc;
}

static void uart_deinit(void)
{
    umcub_log_set_sink(NULL);
    umcub_port_uart_deinit();
    up = false;
}

const umcub_transport_t umcub_transport_uart = {
    .id = UMCUB_TRANSPORT_UART,
    .name = "uart",
    .init = uart_init,
    .deinit = uart_deinit,
    .read = uart_read,
    .write = uart_write,
    .link = UMCUB_CFG_UART_LINK,
};
