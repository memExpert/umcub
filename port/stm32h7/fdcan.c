/*
 * STM32H7 port: FDCAN (Bosch M_CAN) polled driver (RM0399 §56).
 *
 * Message RAM layout (word offsets from SRAMCAN_BASE, FDCAN1 only):
 *   0      1 standard filter
 *   1      1 extended filter (2 words)
 *   4      RX FIFO 0, 8 elements x 18 words (64-byte data field)
 *   148    TX FIFO,   4 elements x 18 words
 */
#include <string.h>
#include "h7.h"
#include "umcub_port_can.h"

#define RAM_WORDS(off)  ((volatile uint32_t *)(SRAMCAN_BASE + (off) * 4u))
#define OFF_SFLT        0u
#define OFF_XFLT        1u
#define OFF_RXF0        4u
#define OFF_TXF         148u
#define ELEM_WORDS      18u
#define RXF0_N          8u
#define TXF_N           4u
#define DS_64           7u     /* data field size code: 64 bytes */

static FDCAN_GlobalTypeDef *can;
static umcub_can_cfg_t cfg;
static const uint8_t dlc_len[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64 };

static uint8_t len_to_dlc(uint8_t len)
{
    uint8_t d = 0;
    while (d < 15 && dlc_len[d] < len) {
        d++;
    }
    return d;
}

/* Prescaler / segments for `bitrate` at `clk`, sample point ~87.5 %
 * (nominal) or 75 % (data). Returns false if not representable. */
static bool timing(uint32_t clk, uint32_t bitrate, uint32_t max_brp, uint32_t max_tq,
                   uint32_t max_seg1, uint32_t max_seg2, unsigned sp_per_mille,
                   uint32_t *brp, uint32_t *seg1, uint32_t *seg2)
{
    for (uint32_t b = 1; b <= max_brp; b++) {
        if (clk % (b * bitrate)) {
            continue;
        }
        uint32_t tq = clk / (b * bitrate);
        if (tq < 8 || tq > max_tq) {
            continue;
        }
        uint32_t s1 = (tq * sp_per_mille) / 1000u - 1u;   /* prop + phase1 */
        uint32_t s2 = tq - 1u - s1;
        if (s1 < 1 || s1 > max_seg1 || s2 < 1 || s2 > max_seg2) {
            continue;
        }
        *brp = b;
        *seg1 = s1;
        *seg2 = s2;
        return true;
    }
    return false;
}

int umcub_port_can_init(const umcub_can_cfg_t *c)
{
    if (c->instance != 1) {
        return UMCUB_EINVAL;    /* FDCAN2 shares the message RAM: not wired up yet */
    }
    if (!h7_pll1q_hz) {
        return UMCUB_EIO;
    }
    cfg = *c;
    can = FDCAN1;

    MODIFY_REG(RCC->D2CCIP1R, RCC_D2CCIP1R_FDCANSEL, 1u << RCC_D2CCIP1R_FDCANSEL_Pos);   /* PLL1Q */
    SET_BIT(RCC->APB1HENR, RCC_APB1HENR_FDCANEN);
    (void)RCC->APB1HENR;
    umcub_cm_periph_used(&RCC->APB1HRSTR, RCC_APB1HRSTR_FDCANRST);

    umcub_port_gpio_af(c->tx_pin);
    umcub_port_gpio_af(c->rx_pin);

    CLEAR_BIT(can->CCCR, FDCAN_CCCR_CSR);
    (void)umcub_cm_wait(&can->CCCR, FDCAN_CCCR_CSA, 0, 10);
    SET_BIT(can->CCCR, FDCAN_CCCR_INIT);
    if (umcub_cm_wait(&can->CCCR, FDCAN_CCCR_INIT, FDCAN_CCCR_INIT, 10) != 0) {
        return UMCUB_ETIMEOUT;
    }
    SET_BIT(can->CCCR, FDCAN_CCCR_CCE);

    uint32_t cccr = can->CCCR & ~(FDCAN_CCCR_FDOE | FDCAN_CCCR_BRSE | FDCAN_CCCR_TEST | FDCAN_CCCR_MON |
                                  FDCAN_CCCR_DAR | FDCAN_CCCR_ASM);
    if (c->fd) {
        cccr |= FDCAN_CCCR_FDOE | FDCAN_CCCR_BRSE;
    }
    if (c->loopback) {
        cccr |= FDCAN_CCCR_TEST | FDCAN_CCCR_MON;
    }
    can->CCCR = cccr;
    if (c->loopback) {
        SET_BIT(can->TEST, FDCAN_TEST_LBCK);
    }

    uint32_t brp, s1, s2;
    if (!timing(h7_pll1q_hz, c->bitrate, 512, 385, 256, 128, 875, &brp, &s1, &s2)) {
        return UMCUB_EINVAL;
    }
    can->NBTP = ((s2 - 1u) << FDCAN_NBTP_NSJW_Pos) | ((brp - 1u) << FDCAN_NBTP_NBRP_Pos) |
                ((s1 - 1u) << FDCAN_NBTP_NTSEG1_Pos) | ((s2 - 1u) << FDCAN_NBTP_NTSEG2_Pos);
    if (c->fd) {
        if (!timing(h7_pll1q_hz, c->data_bitrate, 32, 49, 32, 16, 750, &brp, &s1, &s2)) {
            return UMCUB_EINVAL;
        }
        can->DBTP = ((s2 - 1u) << FDCAN_DBTP_DSJW_Pos) | ((brp - 1u) << FDCAN_DBTP_DBRP_Pos) |
                    ((s1 - 1u) << FDCAN_DBTP_DTSEG1_Pos) | ((s2 - 1u) << FDCAN_DBTP_DTSEG2_Pos) |
                    (c->data_bitrate >= 2000000u ? FDCAN_DBTP_TDC : 0);
        if (c->data_bitrate >= 2000000u) {
            can->TDCR = (((s1 + 1u) * brp) << FDCAN_TDCR_TDCO_Pos);
        }
    }

    /* Message RAM */
    memset((void *)RAM_WORDS(0), 0, (OFF_TXF + TXF_N * ELEM_WORDS) * 4u);
    can->SIDFC = ((OFF_SFLT * 4u) & FDCAN_SIDFC_FLSSA) | (1u << FDCAN_SIDFC_LSS_Pos);
    can->XIDFC = ((OFF_XFLT * 4u) & FDCAN_XIDFC_FLESA) | (1u << FDCAN_XIDFC_LSE_Pos);
    can->RXF0C = ((OFF_RXF0 * 4u) & FDCAN_RXF0C_F0SA) | (RXF0_N << FDCAN_RXF0C_F0S_Pos);
    can->RXF1C = 0;
    can->RXBC = 0;
    can->RXESC = DS_64 << FDCAN_RXESC_F0DS_Pos;
    can->TXEFC = 0;
    can->TXBC = ((OFF_TXF * 4u) & FDCAN_TXBC_TBSA) | (TXF_N << FDCAN_TXBC_TFQS_Pos);
    can->TXESC = DS_64 << FDCAN_TXESC_TBDS_Pos;

    /* One classic id/mask filter to RX FIFO 0, everything else rejected. */
    if (c->ext) {
        RAM_WORDS(OFF_XFLT)[0] = (1u << 29) | (c->rx_id & 0x1FFFFFFFu);
        RAM_WORDS(OFF_XFLT)[1] = (2u << 30) | 0x1FFFFFFFu;
    } else {
        RAM_WORDS(OFF_SFLT)[0] = (2u << 30) | (1u << 27) | ((c->rx_id & 0x7FFu) << 16) | 0x7FFu;
    }
    can->GFC = (2u << FDCAN_GFC_ANFS_Pos) | (2u << FDCAN_GFC_ANFE_Pos) | FDCAN_GFC_RRFS | FDCAN_GFC_RRFE;
    can->IE = 0;
    can->ILE = 0;

    CLEAR_BIT(can->CCCR, FDCAN_CCCR_INIT);
    return umcub_cm_wait(&can->CCCR, FDCAN_CCCR_INIT, 0, 10);
}

void umcub_port_can_deinit(void)
{
    if (!can) {
        return;
    }
    SET_BIT(can->CCCR, FDCAN_CCCR_INIT);
    umcub_port_gpio_reset(cfg.tx_pin);
    umcub_port_gpio_reset(cfg.rx_pin);
    can = NULL;
}

int umcub_port_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    if (!can || len > (cfg.fd ? 64u : 8u)) {
        return UMCUB_EINVAL;
    }
    if (can->TXFQS & FDCAN_TXFQS_TFQF) {
        return UMCUB_EBUSY;
    }
    uint32_t idx = (can->TXFQS & FDCAN_TXFQS_TFQPI) >> FDCAN_TXFQS_TFQPI_Pos;
    volatile uint32_t *e = RAM_WORDS(OFF_TXF + idx * ELEM_WORDS);
    uint8_t dlc = len_to_dlc(len);
    uint8_t buf[64];
    memset(buf, 0xCC, sizeof(buf));     /* ISO-TP padding byte */
    memcpy(buf, data, len);

    e[0] = cfg.ext ? ((1u << 30) | (id & 0x1FFFFFFFu)) : ((id & 0x7FFu) << 18);
    e[1] = ((uint32_t)dlc << 16) | (cfg.fd ? (1u << 21) | (1u << 20) : 0);   /* FDF, BRS */
    for (unsigned w = 0; w < (dlc_len[dlc] + 3u) / 4u; w++) {
        e[2 + w] = (uint32_t)buf[4 * w] | ((uint32_t)buf[4 * w + 1] << 8) |
                   ((uint32_t)buf[4 * w + 2] << 16) | ((uint32_t)buf[4 * w + 3] << 24);
    }
    __DSB();
    can->TXBAR = 1u << idx;
    return 0;
}

bool umcub_port_can_recv(uint32_t *id, uint8_t *data, uint8_t *len)
{
    if (!can) {
        return false;
    }
    if (can->PSR & FDCAN_PSR_BO) {
        CLEAR_BIT(can->CCCR, FDCAN_CCCR_INIT);   /* bus-off recovery */
    }
    if (!(can->RXF0S & FDCAN_RXF0S_F0FL)) {
        return false;
    }
    uint32_t idx = (can->RXF0S & FDCAN_RXF0S_F0GI) >> FDCAN_RXF0S_F0GI_Pos;
    volatile uint32_t *e = RAM_WORDS(OFF_RXF0 + idx * ELEM_WORDS);
    uint32_t r0 = e[0], r1 = e[1];
    *id = (r0 & (1u << 30)) ? (r0 & 0x1FFFFFFFu) : ((r0 >> 18) & 0x7FFu);
    uint8_t n = dlc_len[(r1 >> 16) & 0xFu];
    if (!(r1 & (1u << 21)) && n > 8) {
        n = 8;      /* classic frame: DLC 9..15 mean 8 bytes */
    }
    for (unsigned i = 0; i < n; i++) {
        data[i] = (uint8_t)(e[2 + i / 4u] >> (8u * (i % 4u)));
    }
    *len = n;
    can->RXF0A = idx;
    return true;
}
