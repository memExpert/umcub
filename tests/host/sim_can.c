/*
 * umcub CAN device simulator: the real CAN transport (ISO-TP) + mux +
 * boot_serial on emulated flash, recovery mode. The CAN bus is stdin/stdout in
 * the python-can "serial" interface format, so host tools (tools/smp_can.py)
 * reach it through a pty: 0xAA, timestamp u32 LE, DLC, id u32 LE, data, 0xBB.
 * Used by tests/host/link_e2e.py; exits when stdin is closed.
 */
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fake_port.h"
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "umcub_port_can.h"
#include "umcub_transport.h"

extern uint16_t fake_node_addr;

static uint8_t rx[64];
static size_t rx_len;

int umcub_port_can_init(const umcub_can_cfg_t *cfg)
{
    (void)cfg;
    return 0;
}

void umcub_port_can_deinit(void) {}

int umcub_port_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    uint8_t f[18] = { 0xAA, 0, 0, 0, 0, len, (uint8_t)id, (uint8_t)(id >> 8), (uint8_t)(id >> 16),
                      (uint8_t)(id >> 24) };
    if (len > 8) {
        return UMCUB_EINVAL;            /* classic CAN only in this format */
    }
    memcpy(&f[10], data, len);
    f[10 + len] = 0xBB;
    for (size_t off = 0, n = 11u + len; off < n;) {
        ssize_t w = write(1, f + off, n - off);
        if (w <= 0) {
            exit(0);
        }
        off += (size_t)w;
    }
    return 0;
}

bool umcub_port_can_recv(uint32_t *id, uint8_t *data, uint8_t *len)
{
    struct pollfd p = { .fd = 0, .events = POLLIN };
    while (poll(&p, 1, 0) > 0) {
        ssize_t n = read(0, &rx[rx_len], 1);
        if (n == 0) {
            exit(0);                    /* bus closed */
        }
        if (n < 0) {
            return false;
        }
        if (rx_len == 0 && rx[0] != 0xAA) {
            continue;                   /* resync on the start byte */
        }
        rx_len++;
        if (rx_len >= 6 && (rx[5] > 8 || rx_len == 11u + rx[5])) {
            size_t dlc = rx[5];
            bool ok = dlc <= 8 && rx[10 + dlc] == 0xBB;
            rx_len = 0;
            if (ok) {
                *id = ((uint32_t)rx[6] | (uint32_t)rx[7] << 8 | (uint32_t)rx[8] << 16 |
                       (uint32_t)rx[9] << 24) & 0x1FFFFFFFu;
                memcpy(data, &rx[10], dlc);
                *len = (uint8_t)dlc;
                return true;
            }
        }
    }
    return false;
}

int main(void)
{
    const char *a = getenv("UMCUB_SIM_ADDR");
    fake_realtime = 1;
    fake_node_addr = (uint16_t)(a ? atoi(a) : 0);
    fake_flash_reset();
    umcub_transports_init();
    umcub_recovery_run();
}
