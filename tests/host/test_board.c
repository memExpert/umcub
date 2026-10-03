/*
 * umcub host test: drivers and a transport supplied by the board
 * (UMCUB_DRIVER_BOARD for CAN and Ethernet, UMCUB_CFG_TRANSPORT_USER), run
 * through the real registry, CAN / Ethernet transports and the SMP mux.
 */
#define _GNU_SOURCE     /* memmem */
#include <stdio.h>
#include <string.h>

#include "fake_port.h"
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "umcub_port_can.h"
#include "umcub_port_eth.h"
#include "umcub_transport.h"
#include "umcub_handoff.h"
#include "umcub_log.h"
#include "umcub_cmd.h"
#include "boot_serial/boot_serial.h"

extern const struct boot_uart_funcs *boot_uf;
const struct boot_uart_funcs *umcub_mux_funcs_for_test(void);

static int failures;
#define CHECK(c)                                                                 \
    do {                                                                         \
        if (!(c)) {                                                              \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                \
            failures++;                                                          \
        }                                                                        \
    } while (0)

/* ---- "board" CAN driver (what an MCP2515/MCP2518FD driver would provide) - */
static umcub_can_cfg_t can_cfg;
static int can_inits, can_deinits;
static uint8_t can_rx[64], can_rx_len;
static uint32_t can_rx_id;
static uint8_t can_tx[64], can_tx_len;
static uint32_t can_tx_id;

int umcub_port_can_init(const umcub_can_cfg_t *cfg)
{
    can_cfg = *cfg;
    can_inits++;
    return 0;
}
void umcub_port_can_deinit(void) { can_deinits++; }
int umcub_port_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    can_tx_id = id;
    memcpy(can_tx, data, len);
    can_tx_len = len;
    return 0;
}
bool umcub_port_can_recv(uint32_t *id, uint8_t *data, uint8_t *len)
{
    if (!can_rx_len) {
        return false;
    }
    *id = can_rx_id;
    memcpy(data, can_rx, can_rx_len);
    *len = can_rx_len;
    can_rx_len = 0;
    return true;
}

/* ---- "board" Ethernet driver (ENC28J60-like: raw frames) ----------------- */
static int eth_inits, eth_deinits, eth_link_polls, eth_rx_polls, eth_tx_n;
static const uint32_t *eth_pins = (const uint32_t *)1;
static unsigned eth_npins = 99;
static uint8_t eth_last_tx[600];

int umcub_port_eth_init(const uint8_t mac[6], unsigned phy_addr, const uint32_t *pins, unsigned npins)
{
    (void)mac;
    (void)phy_addr;
    eth_pins = pins;
    eth_npins = npins;
    eth_inits++;
    return 0;
}
void umcub_port_eth_deinit(void) { eth_deinits++; }
bool umcub_port_eth_link(void)
{
    eth_link_polls++;
    return true;
}
int umcub_port_eth_tx(const uint8_t *frame, size_t len)
{
    memcpy(eth_last_tx, frame, len < sizeof(eth_last_tx) ? len : sizeof(eth_last_tx));
    eth_tx_n++;
    return 0;
}
size_t umcub_port_eth_rx(uint8_t *buf, size_t max)
{
    (void)buf;
    (void)max;
    eth_rx_polls++;
    return 0;
}

/* ---- board transport: packet transport (W5500-like UDP socket) ----------- */
extern const umcub_transport_t umcub_transport_user;
static int user_inits, user_deinits, user_polls;
static uint8_t user_in[256], user_out[256];
static size_t user_in_len, user_out_len;

static int user_init(void)
{
    user_inits++;
    return 0;
}
static void user_deinit(void) { user_deinits++; }
static void user_poll(void)
{
    user_polls++;
    if (user_in_len && umcub_smp_packet_rx(&umcub_transport_user, user_in, user_in_len)) {
        user_in_len = 0;
    }
}
static int user_send(const uint8_t *pkt, size_t len)
{
    memcpy(user_out, pkt, len);
    user_out_len = len;
    return 0;
}
const umcub_transport_t umcub_transport_user = {
    .id = UMCUB_TRANSPORT_USER, .name = "user", .init = user_init, .deinit = user_deinit,
    .poll = user_poll, .send_packet = user_send,
};

bool umcub_cmd_user(unsigned id, const char *text, umcub_cmd_reply_t reply)
{
    (void)text;
    if (id != 1) {
        return false;
    }
    reply("hello from the board\r\n");
    return true;
}

/* ---- helpers ------------------------------------------------------------- */

/* SMP echo request {"d": "hi"} (group 0, id 0, write). */
static size_t smp_echo(uint8_t *p, uint8_t seq)
{
    static const uint8_t cbor[] = { 0xA1, 0x61, 'd', 0x62, 'h', 'i' };
    const uint8_t hdr[8] = { 2, 0, 0, sizeof(cbor), 0, 0, seq, 0 };
    memcpy(p, hdr, 8);
    memcpy(p + 8, cbor, sizeof(cbor));
    return 8 + sizeof(cbor);
}

static bool is_echo_rsp(const uint8_t *p, size_t len, uint8_t seq)
{
    return len > 8 && p[0] == 3 && p[4] == 0 && p[5] == 0 && p[6] == seq && p[7] == 0 &&
           memmem(p + 8, len - 8, "hi", 2) != NULL;
}

static void mux_step(void)
{
    char tmp[1100];
    int nl;
    boot_uf->read(tmp, sizeof(tmp), &nl);
}

/* ---- tests --------------------------------------------------------------- */

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    boot_uf = umcub_mux_funcs_for_test();

    printf("[registry] board drivers and board transport are registered\n");
    CHECK(umcub_transport_count == 3);
    CHECK(umcub_transports[0]->id == UMCUB_TRANSPORT_CAN);
    CHECK(umcub_transports[1]->id == UMCUB_TRANSPORT_ETH);
    CHECK(umcub_transports[2] == &umcub_transport_user);
    umcub_transports_init();
    CHECK(can_inits == 1 && eth_inits == 1 && user_inits == 1);
    CHECK(can_cfg.rx_id == UMCUB_CFG_CAN_RX_ID && can_cfg.fd && can_cfg.bitrate == UMCUB_CFG_CAN_BITRATE);
    CHECK(eth_pins == NULL && eth_npins == 0);     /* no RMII pins with a board MAC */

    printf("[board CAN driver] SMP echo over ISO-TP (CAN FD single frame)\n");
    uint8_t pkt[64];
    size_t n = smp_echo(pkt, 7);
    can_rx[0] = 0;                                  /* FD single frame: length in byte 1 */
    can_rx[1] = (uint8_t)n;
    memcpy(can_rx + 2, pkt, n);
    can_rx_len = (uint8_t)(n + 2);
    can_rx_id = UMCUB_CFG_CAN_RX_ID;
    can_tx_len = 0;
    mux_step();
    CHECK(can_tx_len > 2 && can_tx_id == UMCUB_CFG_CAN_TX_ID);
    CHECK(can_tx[0] == 0 && is_echo_rsp(can_tx + 2, can_tx[1], 7));
    CHECK(umcub_recovery_last_transport() == UMCUB_TRANSPORT_CAN);

    printf("[board transport] SMP echo and a text command over umcub_transport_user\n");
    user_in_len = smp_echo(user_in, 9);
    user_out_len = 0;
    mux_step();
    CHECK(user_polls > 0 && is_echo_rsp(user_out, user_out_len, 9));
    CHECK(umcub_recovery_last_transport() == UMCUB_TRANSPORT_USER);
    memcpy(user_in, "hello", 5);
    user_in_len = 5;
    user_out_len = 0;
    mux_step();
    CHECK(user_out_len > 5 && memmem(user_out, user_out_len, "hello from the board", 20) != NULL);

    printf("[board Ethernet driver] link polled, DHCP discover sent through it\n");
    for (int i = 0; i < 50 && eth_tx_n == 0; i++) {
        umcub_port_delay_ms(100);
        mux_step();
    }
    CHECK(eth_link_polls > 0 && eth_rx_polls > 0);
    CHECK(eth_tx_n > 0 && eth_last_tx[12] == 0x08 && eth_last_tx[13] == 0x00 &&   /* IPv4 */
          eth_last_tx[23] == 17 && eth_last_tx[36] == 0 && eth_last_tx[37] == 67); /* UDP to :67 */

    printf("[deinit] every board driver and transport is shut down before the jump\n");
    umcub_transports_deinit();
    CHECK(can_deinits == 1 && eth_deinits == 1 && user_deinits == 1);

    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
