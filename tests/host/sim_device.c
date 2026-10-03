/*
 * umcub device simulator: the real mux + umcub link + boot_serial on emulated
 * flash, recovery mode, one stream transport on stdin/stdout. Used by
 * tests/host/link_e2e.py, which puts several of them on one simulated bus
 * next to tools/umcub_link.py and standard SMP clients.
 *
 *   UMCUB_SIM_ADDR  node address (0 = unassigned)
 *   UMCUB_SIM_UID   number mixed into the UID (distinct devices)
 * Exits when stdin is closed.
 */
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fake_port.h"
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "umcub_transport.h"
#include "umcub_handoff.h"
#include "umcub_cmd.h"

extern uint16_t fake_node_addr;

static uint8_t in_buf[4096];
static size_t in_len, in_pos;

static size_t sim_read(uint8_t *b, size_t max)
{
    if (in_pos == in_len) {
        struct pollfd p = { .fd = 0, .events = POLLIN };
        if (poll(&p, 1, 1) <= 0) {
            return 0;
        }
        ssize_t n = read(0, in_buf, sizeof(in_buf));
        if (n == 0) {
            exit(0);                            /* bus closed */
        }
        if (n < 0) {
            return 0;
        }
        in_len = (size_t)n;
        in_pos = 0;
    }
    size_t n = 0;
    while (n < max && in_pos < in_len) {
        b[n++] = in_buf[in_pos++];
    }
    return n;
}

static void sim_write(const uint8_t *b, size_t len)
{
    while (len) {
        ssize_t n = write(1, b, len);
        if (n <= 0) {
            exit(0);
        }
        b += n;
        len -= (size_t)n;
    }
}

static int nop_init(void) { return 0; }
static void nop_deinit(void) {}

static const umcub_transport_t t_bus = {
    .id = UMCUB_TRANSPORT_UART, .name = "bus", .init = nop_init, .deinit = nop_deinit,
    .read = sim_read, .write = sim_write, .link = UMCUB_CFG_UART_LINK,
};
const umcub_transport_t *const umcub_transports[] = { &t_bus, 0 };
const unsigned umcub_transport_count = 1;
void umcub_transports_poll(void) {}
void umcub_transports_init(void) {}
void umcub_transports_deinit(void) {}

bool umcub_cmd_user(unsigned id, const char *text, umcub_cmd_reply_t reply)
{
    (void)id;
    (void)text;
    (void)reply;
    return false;
}

int main(void)
{
    const char *a = getenv("UMCUB_SIM_ADDR"), *u = getenv("UMCUB_SIM_UID");
    fake_realtime = 1;
    fake_node_addr = (uint16_t)(a ? atoi(a) : 1);
    fake_uid_seed = (uint32_t)(u ? strtoul(u, NULL, 0) : 0);
    fake_flash_reset();
    umcub_recovery_run();
}
