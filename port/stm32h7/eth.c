/*
 * STM32H7 port: Ethernet MAC/DMA (Synopsys GMAC 4.x, RM0399 §58), RMII,
 * polled, generic PHY (BMCR/BMSR/ANAR/ANLPAR). Descriptors and buffers live
 * in the bootloader RAM (AXI SRAM, D-cache off) which the ETH DMA can reach.
 */
#include <string.h>
#include "h7.h"
#include "umcub_port_eth.h"

#define RX_DESC   4u
#define TX_DESC   2u
#define BUF_SIZE  1536u

#define DES3_OWN   (1u << 31)
#define DES3_IOC   (1u << 30)
#define DES3_FD    (1u << 29)
#define DES3_LD    (1u << 28)
#define RDES3_BUF1V (1u << 24)
#define RDES3_ES   (1u << 15)
#define RDES3_DE   (1u << 19)   /* dribble bit error */
#define RDES3_RE   (1u << 20)
#define RDES3_OE   (1u << 21)
#define RDES3_RWT  (1u << 22)
#define RDES3_GP   (1u << 23)
#define RDES3_CE   (1u << 24)   /* CRC error */
#define RDES3_PL   0x7FFFu

#define PHY_BMCR   0
#define PHY_BMSR   1
#define PHY_ANAR   4
#define PHY_ANLPAR 5
#define BMCR_RESET  0x8000u
#define BMCR_ANEN   0x1000u
#define BMCR_ANRST  0x0200u
#define BMSR_LINK   0x0004u
#define BMSR_ANDONE 0x0020u

typedef struct {
    volatile uint32_t des[4];
} desc_t;

static desc_t rx_desc[RX_DESC] __attribute__((aligned(32)));
static desc_t tx_desc[TX_DESC] __attribute__((aligned(32)));
static uint8_t rx_buf[RX_DESC][BUF_SIZE] __attribute__((aligned(32)));
static uint8_t tx_buf[TX_DESC][BUF_SIZE] __attribute__((aligned(32)));
static unsigned rx_idx, tx_idx;
static unsigned phy;
static bool up, link, speed10;
static uint32_t link_checked;
static const uint32_t *eth_pins;
static unsigned eth_npins;

static int mdio(unsigned reg, bool write, uint16_t *val)
{
    if (h7_wait(&ETH->MACMDIOAR, ETH_MACMDIOAR_MB, 0, 5)) {
        return UMCUB_ETIMEOUT;
    }
    if (write) {
        ETH->MACMDIODR = *val;
    }
    /* CR = 4: HCLK 150-250 MHz -> MDC = HCLK / 102 */
    ETH->MACMDIOAR = (phy << ETH_MACMDIOAR_PA_Pos) | (reg << ETH_MACMDIOAR_RDA_Pos) |
                     (4u << ETH_MACMDIOAR_CR_Pos) | (write ? ETH_MACMDIOAR_MOC_WR : ETH_MACMDIOAR_MOC_RD) |
                     ETH_MACMDIOAR_MB;
    if (h7_wait(&ETH->MACMDIOAR, ETH_MACMDIOAR_MB, 0, 5)) {
        return UMCUB_ETIMEOUT;
    }
    if (!write) {
        *val = (uint16_t)ETH->MACMDIODR;
    }
    return 0;
}

static uint16_t phy_read(unsigned reg)
{
    uint16_t v = 0;
    (void)mdio(reg, false, &v);
    return v;
}

static void phy_write(unsigned reg, uint16_t v)
{
    (void)mdio(reg, true, &v);
}

static void rx_arm(unsigned i)
{
    rx_desc[i].des[0] = (uint32_t)rx_buf[i];
    rx_desc[i].des[1] = 0;
    rx_desc[i].des[2] = 0;
    __DMB();
    rx_desc[i].des[3] = DES3_OWN | DES3_IOC | RDES3_BUF1V;
}

int umcub_port_eth_init(const uint8_t mac[6], unsigned phy_addr, const uint32_t *pins, unsigned npins)
{
    phy = phy_addr;
    eth_pins = pins;
    eth_npins = npins;

    /* RMII must be selected before the MAC clocks run (RM0399 §12.3.11). */
    SET_BIT(RCC->APB4ENR, RCC_APB4ENR_SYSCFGEN);
    (void)RCC->APB4ENR;
    MODIFY_REG(SYSCFG->PMCR, SYSCFG_PMCR_EPIS_SEL, SYSCFG_PMCR_EPIS_SEL_2);
    (void)SYSCFG->PMCR;

    for (unsigned i = 0; i < npins; i++) {
        umcub_port_gpio_af(pins[i]);
    }

    SET_BIT(RCC->AHB1ENR, RCC_AHB1ENR_ETH1MACEN | RCC_AHB1ENR_ETH1TXEN | RCC_AHB1ENR_ETH1RXEN);
    (void)RCC->AHB1ENR;
    h7_periph_used(&RCC->AHB1RSTR, RCC_AHB1RSTR_ETH1MACRST);

    SET_BIT(ETH->DMAMR, ETH_DMAMR_SWR);
    if (h7_wait(&ETH->DMAMR, ETH_DMAMR_SWR, 0, 100)) {
        return UMCUB_ETIMEOUT;   /* no 50 MHz REF_CLK from the PHY */
    }

    /* PHY reset + autonegotiation (link settles later, see umcub_port_eth_link). */
    phy_write(PHY_BMCR, BMCR_RESET);
    uint32_t t0 = umcub_port_millis();
    while (phy_read(PHY_BMCR) & BMCR_RESET) {
        if ((uint32_t)(umcub_port_millis() - t0) > 500u) {
            return UMCUB_ETIMEOUT;
        }
    }
    phy_write(PHY_BMCR, BMCR_ANEN | BMCR_ANRST);

    /* MAC */
    ETH->MACA0HR = ((uint32_t)mac[5] << 8) | mac[4];
    ETH->MACA0LR = ((uint32_t)mac[3] << 24) | ((uint32_t)mac[2] << 16) | ((uint32_t)mac[1] << 8) | mac[0];
    ETH->MACPFR = 0;     /* perfect DA filter + broadcast */
    ETH->MACCR = ETH_MACCR_DM | ETH_MACCR_FES | ETH_MACCR_ACS | ETH_MACCR_CST;   /* strip FCS */

    /* MTL: store-and-forward both ways */
    ETH->MTLTQOMR = ETH_MTLTQOMR_TSF;
    ETH->MTLRQOMR = ETH_MTLRQOMR_RSF;

    /* DMA */
    ETH->DMASBMR = ETH_DMASBMR_AAL;
    ETH->DMACCR = 0;
    ETH->DMACTCR = 8u << ETH_DMACTCR_TPBL_Pos;
    ETH->DMACRCR = (8u << ETH_DMACRCR_RPBL_Pos) | (BUF_SIZE << ETH_DMACRCR_RBSZ_Pos);

    memset((void *)tx_desc, 0, sizeof(tx_desc));
    for (unsigned i = 0; i < RX_DESC; i++) {
        rx_arm(i);
    }
    rx_idx = tx_idx = 0;
    ETH->DMACTDLAR = (uint32_t)tx_desc;
    ETH->DMACTDRLR = TX_DESC - 1u;
    ETH->DMACTDTPR = (uint32_t)tx_desc;
    ETH->DMACRDLAR = (uint32_t)rx_desc;
    ETH->DMACRDRLR = RX_DESC - 1u;
    ETH->DMACRDTPR = (uint32_t)&rx_desc[RX_DESC - 1u];

    ETH->DMACTCR |= ETH_DMACTCR_ST;
    ETH->DMACRCR |= ETH_DMACRCR_SR;
    ETH->MACCR |= ETH_MACCR_TE | ETH_MACCR_RE;

    up = true;
    link = false;
    link_checked = umcub_port_millis() - 1000u;
    return 0;
}

void umcub_port_eth_deinit(void)
{
    if (!up) {
        return;
    }
    ETH->DMACTCR &= ~ETH_DMACTCR_ST;
    ETH->MACCR &= ~(ETH_MACCR_TE | ETH_MACCR_RE);
    ETH->DMACRCR &= ~ETH_DMACRCR_SR;
    for (unsigned i = 0; i < eth_npins; i++) {
        umcub_port_gpio_reset(eth_pins[i]);
    }
    up = false;
}

bool umcub_port_eth_link(void)
{
    if (!up) {
        return false;
    }
    if ((uint32_t)(umcub_port_millis() - link_checked) < 250u) {
        return link;
    }
    link_checked = umcub_port_millis();
    (void)phy_read(PHY_BMSR);                       /* link bit is latched low */
    uint16_t bmsr = phy_read(PHY_BMSR);
    bool now = (bmsr & BMSR_LINK) && (bmsr & BMSR_ANDONE);
    if (now && !link) {
        uint16_t common = phy_read(PHY_ANAR) & phy_read(PHY_ANLPAR);
        uint32_t cr = ETH->MACCR & ~(ETH_MACCR_DM | ETH_MACCR_FES);
        if (common & 0x0100u) {            /* 100BASE-TX FD */
            cr |= ETH_MACCR_DM | ETH_MACCR_FES;
        } else if (common & 0x0080u) {     /* 100BASE-TX HD */
            cr |= ETH_MACCR_FES;
        } else if (common & 0x0040u) {     /* 10BASE-T FD */
            cr |= ETH_MACCR_DM;
        }
        ETH->MACCR = cr;
        /* ES0445 2.25.10: in RMII 10 Mbit/s the MAC may flag a fake dribble
         * nibble together with a CRC error and drop good packets. Forward
         * error packets and accept "DE + CE only" ones (IP/UDP checksums
         * still reject real corruption). */
        speed10 = !(cr & ETH_MACCR_FES);
        if (speed10) {
            ETH->MTLRQOMR |= ETH_MTLRQOMR_FEP;
        } else {
            ETH->MTLRQOMR &= ~ETH_MTLRQOMR_FEP;
        }
    }
    link = now;
    return link;
}

int umcub_port_eth_tx(const uint8_t *frame, size_t len)
{
    if (!up || len > BUF_SIZE) {
        return UMCUB_EINVAL;
    }
    desc_t *d = &tx_desc[tx_idx];
    uint32_t t0 = umcub_port_millis();
    while (d->des[3] & DES3_OWN) {
        if ((uint32_t)(umcub_port_millis() - t0) > 20u) {
            return UMCUB_EBUSY;
        }
    }
    memcpy(tx_buf[tx_idx], frame, len);
    if (len < 60) {
        memset(&tx_buf[tx_idx][len], 0, 60 - len);   /* MAC pads too; keep it explicit */
        len = 60;
    }
    d->des[0] = (uint32_t)tx_buf[tx_idx];
    d->des[1] = 0;
    d->des[2] = (uint32_t)len;
    __DMB();
    d->des[3] = DES3_OWN | DES3_FD | DES3_LD | (uint32_t)len;
    __DSB();
    tx_idx = (tx_idx + 1u) % TX_DESC;
    ETH->DMACTDTPR = (uint32_t)&tx_desc[tx_idx];
    return 0;
}

size_t umcub_port_eth_rx(uint8_t *buf, size_t max)
{
    if (!up) {
        return 0;
    }
    desc_t *d = &rx_desc[rx_idx];
    uint32_t s = d->des[3];
    if (s & DES3_OWN) {
        return 0;
    }
    size_t len = 0;
    uint32_t errs = s & (RDES3_DE | RDES3_RE | RDES3_OE | RDES3_RWT | RDES3_GP | RDES3_CE);
    bool ok = !(s & RDES3_ES) || (speed10 && errs == (RDES3_DE | RDES3_CE));
    if ((s & DES3_FD) && (s & DES3_LD) && ok) {
        len = s & RDES3_PL;                        /* FCS stripped by the MAC (CST) */
        if (len > max) {
            len = 0;
        } else {
            memcpy(buf, rx_buf[rx_idx], len);
        }
    }
    rx_arm(rx_idx);
    __DSB();
    ETH->DMACRDTPR = (uint32_t)d;
    rx_idx = (rx_idx + 1u) % RX_DESC;
    /* Recover from "receive buffer unavailable". */
    if (ETH->DMACSR & ETH_DMACSR_RBU) {
        ETH->DMACSR = ETH_DMACSR_RBU;
    }
    return len;
}
