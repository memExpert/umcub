/*
 * UART transport: SMP serial (NLIP framing) + log output.
 */
#include "umcub_cfg.h"
#include "umcub_log.h"
#include "umcub_port.h"
#include "umcub_port_uart.h"
#include "umcub_transport.h"
#include "umcub_handoff.h"

static void log_sink(const char *s, size_t len)
{
    umcub_port_uart_write((const uint8_t *)s, len);
}

static bool up;

static int uart_init(void)
{
    if (up) {
        return 0;
    }
    int rc = umcub_port_uart_init(UMCUB_CFG_UART_INSTANCE, UMCUB_CFG_UART_BAUD,
                                  UMCUB_CFG_UART_TX_PIN, UMCUB_CFG_UART_RX_PIN);
    if (rc == 0) {
        up = true;
        umcub_log_set_sink(log_sink);
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
    .read = umcub_port_uart_read,
    .write = umcub_port_uart_write,
};
