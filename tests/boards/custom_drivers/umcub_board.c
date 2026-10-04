/*
 * Stub board drivers for the build matrix (see umcub_config.h). A real board
 * talks to its external controllers here; LL / register access is allowed in
 * board code.
 */
#include "umcub_cfg.h"
#include "umcub_cmd.h"
#include "umcub_port.h"
#include "umcub_port_can.h"
#include "umcub_port_eth.h"
#include "umcub_transport.h"
#include "umcub_handoff.h"
#include "stm32h7xx.h"

bool umcub_cmd_user(unsigned id, const char *text, umcub_cmd_reply_t reply)
{
    (void)id;
    (void)text;
    (void)reply;
    return false;
}

/* CAN controller on SPI (MCP2515 / MCP2518FD) */
int umcub_port_can_init(const umcub_can_cfg_t *cfg) { (void)cfg; return UMCUB_EIO; }
void umcub_port_can_deinit(void) {}
int umcub_port_can_send(uint32_t id, const uint8_t *data, uint8_t len) { (void)id; (void)data; (void)len; return UMCUB_EIO; }
bool umcub_port_can_recv(uint32_t *id, uint8_t *data, uint8_t *len) { (void)id; (void)data; (void)len; return false; }

/* Ethernet MAC on SPI (ENC28J60 / LAN9250) */
int umcub_port_eth_init(const uint8_t mac[6], unsigned phy, const uint32_t *pins, unsigned npins)
{
    (void)mac; (void)phy; (void)pins; (void)npins;
    return UMCUB_EIO;
}
void umcub_port_eth_deinit(void) {}
bool umcub_port_eth_link(void) { return false; }
int umcub_port_eth_tx(const uint8_t *frame, size_t len) { (void)frame; (void)len; return UMCUB_EIO; }
size_t umcub_port_eth_rx(uint8_t *buf, size_t max) { (void)buf; (void)max; return 0; }

/* Board transport */
static int user_init(void) { return SPI1 ? 0 : UMCUB_EIO; }   /* LL/CMSIS visible here */
static void user_deinit(void) {}
static int user_send(const uint8_t *pkt, size_t len) { (void)pkt; (void)len; return 0; }
const umcub_transport_t umcub_transport_user = {
    .id = UMCUB_TRANSPORT_USER, .name = "user", .init = user_init, .deinit = user_deinit,
    .send_packet = user_send,
};

/* Board protocol (UMCUB_CFG_PROTO_USER): frames starting with 'Z' write a
 * slot through umcub_slot_* - here just answered, as a build check. */
bool umcub_proto_user(const umcub_transport_t *t, const uint8_t *data, size_t len)
{
    if (len == 0 || data[0] != 'Z') {
        return false;
    }
    static const uint8_t ok[] = { 'Z', 0 };
    umcub_proto_reply(t, ok, sizeof(ok));
    return true;
}
