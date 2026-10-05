/*
 * STM32F1 port: bxCAN (CAN1) polled driver, classic CAN (RM0008 §24).
 *
 * Pins by the TX pin, like the UART: PA12/PA11 (default), PB9/PB8 (AFIO
 * CAN_REMAP = 10), PD1/PD0 (CAN_REMAP = 11). One filter bank in identifier
 * list mode passes UMCUB_CFG_CAN_RX_ID into FIFO 0. On F103 parts CAN and
 * USB share 512 bytes of SRAM: they cannot be used together (config check).
 */
#include "f1.h"
#include "umcub_port_can.h"

#define PIN(port, num) UMCUB_PIN(port, num, 0)

static const struct {
    uint32_t tx, rx, remap;
} map[] = {
    { PIN('A', 12), PIN('A', 11), 0 },
    { PIN('B', 9), PIN('B', 8), AFIO_MAPR_CAN_REMAP_REMAP2 },
    { PIN('D', 1), PIN('D', 0), AFIO_MAPR_CAN_REMAP_REMAP3 },    /* 100/144-pin packages only */
};

static umcub_can_cfg_t cfg;

/* Prescaler and segments for `bitrate` (sample point ~87.5 %): 8..25 time
 * quanta, TS1 1..16, TS2 1..8, BRP 1..1024 (RM0008 §24.7.7). */
static bool timing(uint32_t clk, uint32_t bitrate, uint32_t *btr)
{
    for (uint32_t brp = 1; brp <= 1024; brp++) {
        if (clk % (brp * bitrate)) {
            continue;
        }
        uint32_t tq = clk / (brp * bitrate);
        if (tq < 8 || tq > 25) {
            continue;
        }
        uint32_t ts1 = (tq * 875u + 999u) / 1000u - 1u;    /* rounded up: sample point >= 87.5 % */
        uint32_t ts2 = tq - 1u - ts1;
        if (ts1 < 1 || ts1 > 16 || ts2 < 1 || ts2 > 8) {
            continue;
        }
        *btr = (brp - 1u) | (ts1 - 1u) << CAN_BTR_TS1_Pos | (ts2 - 1u) << CAN_BTR_TS2_Pos |
               (ts2 > 4 ? 3u : ts2 - 1u) << CAN_BTR_SJW_Pos;
        return true;
    }
    return false;
}

/* Identifier register layout (TIxR / RIxR / filter): STID 31:21, EXID 31:3, IDE 2. */
static uint32_t id_reg(uint32_t id, bool ext)
{
    return ext ? (id << 3) | CAN_TI0R_IDE : id << 21;
}

int umcub_port_can_init(const umcub_can_cfg_t *c)
{
    unsigned m = 0;
    while (m < sizeof(map) / sizeof(map[0]) && map[m].tx != c->tx_pin) {
        m++;
    }
    uint32_t btr;
    if (c->instance != 1 || c->fd || m == sizeof(map) / sizeof(map[0]) || map[m].rx != c->rx_pin ||
        !timing(f1_pclk1_hz, c->bitrate, &btr)) {
        return UMCUB_EINVAL;
    }
    cfg = *c;

    SET_BIT(RCC->APB1ENR, RCC_APB1ENR_CAN1EN);
    (void)RCC->APB1ENR;
    umcub_cm_periph_used(&RCC->APB1RSTR, RCC_APB1RSTR_CAN1RST);
    if (map[m].remap) {
        SET_BIT(RCC->APB2ENR, RCC_APB2ENR_AFIOEN);
        umcub_cm_periph_used(&RCC->APB2RSTR, RCC_APB2RSTR_AFIORST);
        /* SWJ_CFG reads back undefined: write 111 ("no effect") with the remap
         * bits, or SWD may get switched off (see uart.c). */
        MODIFY_REG(AFIO->MAPR, AFIO_MAPR_CAN_REMAP | AFIO_MAPR_SWJ_CFG, map[m].remap | AFIO_MAPR_SWJ_CFG);
    }
    umcub_port_gpio_af(c->tx_pin);
    umcub_port_gpio_input(c->rx_pin, UMCUB_PULL_UP);   /* recessive while no transceiver drives it */

    CAN_TypeDef *can = CAN1;
    CLEAR_BIT(can->MCR, CAN_MCR_SLEEP);
    SET_BIT(can->MCR, CAN_MCR_INRQ);
    if (umcub_cm_wait(&can->MSR, CAN_MSR_INAK | CAN_MSR_SLAK, CAN_MSR_INAK, 10)) {
        return UMCUB_ETIMEOUT;
    }
    /* Automatic bus-off recovery, transmit in request order. */
    MODIFY_REG(can->MCR, CAN_MCR_ABOM | CAN_MCR_TXFP, CAN_MCR_ABOM | CAN_MCR_TXFP);
    can->BTR = btr | (c->loopback ? CAN_BTR_LBKM | CAN_BTR_SILM : 0u);

    /* Filter bank 0: 32-bit identifier list, both entries = rx_id, FIFO 0. */
    SET_BIT(can->FMR, CAN_FMR_FINIT);
    CLEAR_BIT(can->FA1R, 1u);
    SET_BIT(can->FS1R, 1u);
    SET_BIT(can->FM1R, 1u);
    CLEAR_BIT(can->FFA1R, 1u);
    can->sFilterRegister[0].FR1 = id_reg(c->rx_id, c->ext);
    can->sFilterRegister[0].FR2 = id_reg(c->rx_id, c->ext);
    SET_BIT(can->FA1R, 1u);
    CLEAR_BIT(can->FMR, CAN_FMR_FINIT);

    /* Leave initialisation: needs 11 recessive bits on CAN_RX. */
    CLEAR_BIT(can->MCR, CAN_MCR_INRQ);
    return umcub_cm_wait(&can->MSR, CAN_MSR_INAK, 0, 50) ? UMCUB_ETIMEOUT : UMCUB_OK;
}

void umcub_port_can_deinit(void)
{
    /* The controller is reset with the other used peripherals (umcub_cm_deinit()). */
    if (cfg.tx_pin) {
        umcub_port_gpio_reset(cfg.tx_pin);
        umcub_port_gpio_reset(cfg.rx_pin);
    }
}

int umcub_port_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    CAN_TypeDef *can = CAN1;
    uint32_t tsr = can->TSR;
    if (len > 8) {
        return UMCUB_EINVAL;
    }
    if (!(tsr & (CAN_TSR_TME0 | CAN_TSR_TME1 | CAN_TSR_TME2))) {
        return UMCUB_EBUSY;
    }
    CAN_TxMailBox_TypeDef *mb = &can->sTxMailBox[(tsr & CAN_TSR_CODE) >> CAN_TSR_CODE_Pos];
    uint8_t b[8] = { 0 };
    for (unsigned i = 0; i < len; i++) {
        b[i] = data[i];
    }
    mb->TDTR = len;
    mb->TDLR = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    mb->TDHR = (uint32_t)b[4] | (uint32_t)b[5] << 8 | (uint32_t)b[6] << 16 | (uint32_t)b[7] << 24;
    mb->TIR = id_reg(id, cfg.ext) | CAN_TI0R_TXRQ;
    return UMCUB_OK;
}

bool umcub_port_can_recv(uint32_t *id, uint8_t *data, uint8_t *len)
{
    CAN_TypeDef *can = CAN1;
    if (!(can->RF0R & CAN_RF0R_FMP0)) {
        return false;
    }
    CAN_FIFOMailBox_TypeDef *mb = &can->sFIFOMailBox[0];
    uint32_t rir = mb->RIR;
    *id = (rir & CAN_RI0R_IDE) ? rir >> 3 : rir >> 21;
    *len = (uint8_t)(mb->RDTR & CAN_RDT0R_DLC);
    if (*len > 8) {
        *len = 8;
    }
    uint32_t w[2] = { mb->RDLR, mb->RDHR };
    for (unsigned i = 0; i < *len; i++) {
        data[i] = (uint8_t)(w[i / 4] >> (8 * (i % 4)));
    }
    can->RF0R = CAN_RF0R_RFOM0;     /* release the output mailbox (RFOM0 is set-only) */
    return true;
}
