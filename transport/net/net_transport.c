/*
 * Ethernet transport: SMP over UDP (mcumgr --conntype udp, port
 * UMCUB_CFG_ETH_SMP_PORT) on the minimal IPv4 stack.
 */
#include <string.h>
#include "net.h"
#include "umcub_cfg.h"
#include "umcub_log.h"
#include "umcub_port.h"
#include "umcub_port_eth.h"
#include "umcub_transport.h"
#include "umcub_handoff.h"

extern const umcub_transport_t umcub_transport_eth;

#ifdef UMCUB_CFG_ETH_RMII_PINS
static const uint32_t rmii_pins[] = { UMCUB_CFG_ETH_RMII_PINS };
#define RMII_PINS rmii_pins, sizeof(rmii_pins) / sizeof(rmii_pins[0])
#else
#define RMII_PINS NULL, 0u      /* board driver (UMCUB_DRIVER_BOARD) with its own wiring */
#endif
static uint8_t frame[1536];
static net_peer_t smp_peer;
static bool up, link;

static void make_mac(uint8_t mac[6])
{
#if UMCUB_CFG_ETH_MAC
    for (int i = 0; i < 6; i++) {
        mac[i] = (uint8_t)((uint64_t)UMCUB_CFG_ETH_MAC >> (8 * (5 - i)));
    }
#else
    uint8_t uid[12];
    umcub_port_uid(uid);
    uint32_t h = umcub_crc32(uid, sizeof(uid));
    mac[0] = 0x02;              /* locally administered, unicast */
    mac[1] = 0x55;
    mac[2] = (uint8_t)(h >> 24);
    mac[3] = (uint8_t)(h >> 16);
    mac[4] = (uint8_t)(h >> 8);
    mac[5] = (uint8_t)h;
#endif
}

static int eth_init(void)
{
    if (up) {
        return 0;
    }
    uint8_t mac[6];
    make_mac(mac);
    int rc = umcub_port_eth_init(mac, UMCUB_CFG_ETH_PHY_ADDR, RMII_PINS);
    if (rc) {
        UMCUB_LOG_ERR("eth: init failed %d", rc);
        return rc;
    }
    net_init(mac);
    up = true;
    link = false;
    UMCUB_LOG_INF("eth: mac %02x:%02x:%02x:%02x:%02x:%02x, SMP on udp/%d", mac[0], mac[1], mac[2],
                  mac[3], mac[4], mac[5], UMCUB_CFG_ETH_SMP_PORT);
    return 0;
}

static void eth_deinit(void)
{
    if (up) {
        umcub_port_eth_deinit();
        up = false;
    }
}

static void eth_poll(void)
{
    if (!up) {
        return;
    }
    bool l = umcub_port_eth_link();
    if (l != link) {
        link = l;
        UMCUB_LOG_INF("eth: link %s", l ? "up" : "down");
        if (l) {
            dhcp_start();
        }
    }
    if (!link) {
        return;
    }
    size_t n;
    for (int budget = 4; budget && (n = umcub_port_eth_rx(frame, sizeof(frame))) > 0; budget--) {
        net_input(frame, n);
    }
    dhcp_tick();
}

void net_udp_input(const net_peer_t *from, uint16_t dst_port, const uint8_t *data, size_t len)
{
    if (dst_port != UMCUB_CFG_ETH_SMP_PORT) {
        return;
    }
    if (umcub_smp_packet_rx(&umcub_transport_eth, data, len)) {
        smp_peer = *from;
    }
}

static int eth_send_packet(const uint8_t *pkt, size_t len)
{
    return up ? net_udp_send(&smp_peer, UMCUB_CFG_ETH_SMP_PORT, pkt, len) : UMCUB_EIO;
}

const umcub_transport_t umcub_transport_eth = {
    .id = UMCUB_TRANSPORT_ETH,
    .name = "eth",
    .init = eth_init,
    .deinit = eth_deinit,
    .poll = eth_poll,
    .send_packet = eth_send_packet,
};
