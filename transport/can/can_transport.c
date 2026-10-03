/*
 * CAN / CAN-FD transport: raw SMP packets over ISO-TP.
 *   host -> device: UMCUB_CFG_CAN_RX_ID, device -> host: UMCUB_CFG_CAN_TX_ID
 * Host side: tools/smp_can.py (python-can).
 */
#include "umcub_cfg.h"
#include "umcub_log.h"
#include "umcub_port.h"
#include "umcub_port_can.h"
#include "umcub_transport.h"
#include "umcub_handoff.h"
#include "isotp.h"

extern const umcub_transport_t umcub_transport_can;

static uint8_t rx_msg[UMCUB_CFG_SMP_MTU];
static isotp_t tp;
static bool up;

static int can_init(void)
{
    if (up) {
        return 0;
    }
    const umcub_can_cfg_t c = {
        .instance = UMCUB_CFG_CAN_INSTANCE,
        .bitrate = UMCUB_CFG_CAN_BITRATE,
        .data_bitrate = UMCUB_CFG_CAN_DATA_BITRATE,
        .fd = UMCUB_CFG_CAN_FD,
        .loopback = UMCUB_CFG_CAN_LOOPBACK,
        .rx_id = UMCUB_CFG_CAN_RX_ID,
        .ext = UMCUB_CFG_CAN_EXT_ID,
        .tx_pin = UMCUB_CFG_CAN_TX_PIN,
        .rx_pin = UMCUB_CFG_CAN_RX_PIN,
    };
    int rc = umcub_port_can_init(&c);
    if (rc) {
        UMCUB_LOG_ERR("can: init failed %d", rc);
        return rc;
    }
    tp.frame_len = UMCUB_CFG_CAN_FD ? 64 : 8;
    tp.tx_id = UMCUB_CFG_CAN_TX_ID;
    tp.rx_buf = rx_msg;
    tp.rx_cap = sizeof(rx_msg);
    up = true;
    UMCUB_LOG_INF("can: %lu bit/s%s, rx 0x%lx tx 0x%lx", (unsigned long)UMCUB_CFG_CAN_BITRATE,
                  UMCUB_CFG_CAN_FD ? " FD" : "", (unsigned long)UMCUB_CFG_CAN_RX_ID,
                  (unsigned long)UMCUB_CFG_CAN_TX_ID);
    return 0;
}

static void can_deinit(void)
{
    if (up) {
        umcub_port_can_deinit();
        up = false;
    }
}

static void can_poll(void)
{
    uint32_t id;
    uint8_t d[64], len;
    while (up && umcub_port_can_recv(&id, d, &len)) {
        size_t n = isotp_on_frame(&tp, d, len);
        if (n) {
            (void)umcub_smp_packet_rx(&umcub_transport_can, rx_msg, n);
        }
    }
}

static int can_send_packet(const uint8_t *pkt, size_t len)
{
    return up ? isotp_send(&tp, pkt, len) : UMCUB_EIO;
}

const umcub_transport_t umcub_transport_can = {
    .id = UMCUB_TRANSPORT_CAN,
    .name = "can",
    .init = can_init,
    .deinit = can_deinit,
    .poll = can_poll,
    .send_packet = can_send_packet,
};
