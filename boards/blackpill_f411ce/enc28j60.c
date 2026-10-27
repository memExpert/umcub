/*
 * ENC28J60 (Microchip, 10BASE-T MAC + PHY on SPI) as the board's Ethernet
 * driver under the built-in IPv4 / UDP transport: UMCUB_CFG_ETH_DRIVER =
 * UMCUB_DRIVER_BOARD (variants/enc28j60.h), the umcub_port_eth_* interface of
 * umcub_port_eth.h. Polled, no interrupt pin; SPI1 on registers.
 *
 * Datasheet DS39662, errata DS80349 (rev. B7) - what this driver relies on:
 *  - SCK at least 8 MHz for MAC / MII register reads, at most 20 MHz;
 *  - after a reset wait 1 ms before the first access (CLKRDY is not reliable);
 *  - ERXRDPT is written with an odd value (next packet - 1);
 *  - a pending packet is EPKTCNT > 0, not EIR.PKTIF;
 *  - the transmit logic is reset before each frame (it can stall after a
 *    collision) and an unfinished transmission is aborted after a timeout.
 * Half duplex: the link partner falls back to it by parallel detection.
 */
#include "umcub_cfg.h"

#if UMCUB_CFG_TRANSPORT_ETH && UMCUB_CFG_ETH_DRIVER == UMCUB_DRIVER_BOARD
#include "umcub_port.h"
#include "umcub_port_eth.h"
#include "stm32f4xx.h"

/* SPI opcodes */
#define OP_RCR  0x00u
#define OP_RBM  0x3Au
#define OP_WCR  0x40u
#define OP_WBM  0x7Au
#define OP_BFS  0x80u
#define OP_BFC  0xA0u
#define OP_SRC  0xFFu

/* Registers: bits 0-4 address, bits 5-6 bank, bit 7 MAC/MII (dummy byte on read). */
#define R(bank, addr)   ((uint8_t)((bank) << 5 | (addr)))
#define MACMII          0x80u
#define ERDPTL      R(0, 0x00)
#define EWRPTL      R(0, 0x02)
#define ETXSTL      R(0, 0x04)
#define ETXNDL      R(0, 0x06)
#define ERXSTL      R(0, 0x08)
#define ERXNDL      R(0, 0x0A)
#define ERXRDPTL    R(0, 0x0C)
#define ERXFCON     R(1, 0x18)
#define EPKTCNT     R(1, 0x19)
#define MACON1      (R(2, 0x00) | MACMII)
#define MACON3      (R(2, 0x02) | MACMII)
#define MACON4      (R(2, 0x03) | MACMII)
#define MABBIPG     (R(2, 0x04) | MACMII)
#define MAIPGL      (R(2, 0x06) | MACMII)
#define MAIPGH      (R(2, 0x07) | MACMII)
#define MAMXFLL     (R(2, 0x0A) | MACMII)
#define MICMD       (R(2, 0x12) | MACMII)
#define MIREGADR    (R(2, 0x14) | MACMII)
#define MIWRL       (R(2, 0x16) | MACMII)
#define MIRDL       (R(2, 0x18) | MACMII)
#define MAADR5      (R(3, 0x00) | MACMII)
#define MAADR6      (R(3, 0x01) | MACMII)
#define MAADR3      (R(3, 0x02) | MACMII)
#define MAADR4      (R(3, 0x03) | MACMII)
#define MAADR1      (R(3, 0x04) | MACMII)
#define MAADR2      (R(3, 0x05) | MACMII)
#define MISTAT      (R(3, 0x0A) | MACMII)
#define EREVID      R(3, 0x12)
/* in every bank */
#define EIR         0x1Cu
#define ESTAT       0x1Du
#define ECON2       0x1Eu
#define ECON1       0x1Fu

#define ECON1_TXRST     0x80u
#define ECON1_RXRST     0x40u
#define ECON1_TXRTS     0x08u
#define ECON1_RXEN      0x04u
#define ECON1_BSEL      0x03u
#define ECON2_AUTOINC   0x80u
#define ECON2_PKTDEC    0x40u
#define ESTAT_CLKRDY    0x01u
#define EIR_TXERIF      0x02u
#define EIR_TXIF        0x08u
#define MISTAT_BUSY     0x01u

/* PHY registers (through MIREGADR / MICMD) */
#define PHCON1          0x00u
#define PHSTAT2         0x11u
#define PHCON2          0x10u
#define PHSTAT2_LSTAT   0x0400u
#define PHCON2_HDLDIS   0x0100u

/* 8 KiB buffer: receive ring from 0 (errata), one transmit frame at the end. */
#define RX_START    0x0000u
#define RX_END      0x19FFu
#define TX_START    0x1A00u
#define FRAME_MAX   1518u

#define SPI_WAIT    10000u      /* polls per SPI byte (a byte takes < 1 us) */

static uint8_t bank;
static uint16_t next_packet;
static uint32_t link_at;
static bool link_up;

/* --- SPI1 ------------------------------------------------------------------ */

static void cs(bool active)
{
    umcub_port_gpio_write(ENC28J60_CS_PIN, !active);
}

static uint8_t xfer(uint8_t b)
{
    for (uint32_t n = SPI_WAIT; !(SPI1->SR & SPI_SR_TXE) && n; n--) {
    }
    *(volatile uint8_t *)&SPI1->DR = b;
    for (uint32_t n = SPI_WAIT; !(SPI1->SR & SPI_SR_RXNE) && n; n--) {
    }
    return *(volatile uint8_t *)&SPI1->DR;
}

static void spi_end(void)
{
    for (uint32_t n = SPI_WAIT; (SPI1->SR & SPI_SR_BSY) && n; n--) {
    }
    cs(false);
}

/* SCK = PCLK2 / 2^(br+1): the fastest at most 20 MHz (>= 8 MHz needed for
 * MAC / MII reads: 12 MHz at 96 MHz, 8 MHz on the 16 MHz HSI fallback). */
static uint32_t spi_br(void)
{
    uint32_t br = 0;
    while (br < 7 && (SystemCoreClock >> (br + 1)) > 20000000u) {
        br++;
    }
    return br << SPI_CR1_BR_Pos;
}

/* --- registers --------------------------------------------------------------- */

static void op(uint8_t opcode, uint8_t addr, uint8_t data)
{
    cs(true);
    (void)xfer((uint8_t)(opcode | (addr & 0x1Fu)));
    (void)xfer(data);
    spi_end();
}

static void select_bank(uint8_t reg)
{
    uint8_t b = (uint8_t)(reg >> 5 & 3u);
    if ((reg & 0x1Fu) < 0x1Bu && b != bank) {
        op(OP_BFC, ECON1, ECON1_BSEL);
        op(OP_BFS, ECON1, b);
        bank = b;
    }
}

static uint8_t rd(uint8_t reg)
{
    select_bank(reg);
    cs(true);
    (void)xfer((uint8_t)(OP_RCR | (reg & 0x1Fu)));
    if (reg & MACMII) {
        (void)xfer(0);                      /* MAC / MII: a dummy byte first */
    }
    uint8_t v = xfer(0);
    spi_end();
    return v;
}

static void wr(uint8_t reg, uint8_t v)
{
    select_bank(reg);
    op(OP_WCR, reg, v);
}

static void wr16(uint8_t reg_l, uint16_t v)
{
    wr(reg_l, (uint8_t)v);
    wr((uint8_t)(reg_l + 1u), (uint8_t)(v >> 8));
}

static bool mii_wait(void)
{
    for (uint32_t t0 = umcub_port_millis(); rd(MISTAT) & MISTAT_BUSY;) {
        if (umcub_port_millis() - t0 > 2u) {
            return false;
        }
    }
    return true;
}

static uint16_t phy_read(uint8_t reg)
{
    wr(MIREGADR, reg);
    wr(MICMD, 0x01);                        /* MIIRD */
    umcub_port_delay_us(12);                /* 10.24 us per MII read */
    (void)mii_wait();
    wr(MICMD, 0);
    return (uint16_t)(rd(MIRDL) | rd((uint8_t)(MIRDL + 1u)) << 8);
}

static void phy_write(uint8_t reg, uint16_t v)
{
    wr(MIREGADR, reg);
    wr16(MIWRL, v);                         /* the high byte starts the write */
    umcub_port_delay_us(12);
    (void)mii_wait();
}

static void buf_read(uint8_t *p, size_t n)
{
    cs(true);
    (void)xfer(OP_RBM);
    while (n--) {
        *p++ = xfer(0);
    }
    spi_end();
}

static void buf_write(const uint8_t *p, size_t n)
{
    cs(true);
    (void)xfer(OP_WBM);
    while (n--) {
        (void)xfer(*p++);
    }
    spi_end();
}

/* --- umcub_port_eth.h -------------------------------------------------------- */

int umcub_port_eth_init(const uint8_t mac[6], unsigned phy_addr, const uint32_t *pins, unsigned npins)
{
    (void)phy_addr;                         /* the PHY is inside the chip */
    (void)pins;
    (void)npins;
    umcub_port_gpio_output(ENC28J60_CS_PIN, true);
#ifdef ENC28J60_RESET_PIN
    umcub_port_gpio_output(ENC28J60_RESET_PIN, false);
    umcub_port_delay_ms(1);
    umcub_port_gpio_write(ENC28J60_RESET_PIN, true);
#endif
    umcub_port_gpio_af(ENC28J60_SCK_PIN);
    umcub_port_gpio_af(ENC28J60_MISO_PIN);
    umcub_port_gpio_af(ENC28J60_MOSI_PIN);
    SET_BIT(RCC->APB2ENR, RCC_APB2ENR_SPI1EN);
    (void)READ_BIT(RCC->APB2ENR, RCC_APB2ENR_SPI1EN);
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | spi_br();    /* mode 0, 8 bit, MSB first */
    SET_BIT(SPI1->CR1, SPI_CR1_SPE);

    cs(true);
    (void)xfer(OP_SRC);                     /* soft reset */
    spi_end();
    umcub_port_delay_ms(2);                 /* errata: CLKRDY may be set too early */
    bank = 0;
    for (uint32_t t0 = umcub_port_millis(); !(rd(ESTAT) & ESTAT_CLKRDY);) {
        if (umcub_port_millis() - t0 > 10u) {
            return UMCUB_ETIMEOUT;          /* no chip, or not powered */
        }
    }
    uint8_t rev = rd(EREVID);
    if (rev == 0x00 || rev == 0xFF) {
        return UMCUB_EIO;
    }

    next_packet = RX_START;
    wr16(ERXSTL, RX_START);
    wr16(ERXNDL, RX_END);
    wr16(ERXRDPTL, RX_END);                 /* odd, "nothing read yet" */
    wr16(ETXSTL, TX_START);
    wr(ERXFCON, 0xA1);                      /* UCEN | CRCEN | BCEN: ours and broadcast, CRC good */

    wr(MACON1, 0x01);                       /* MARXEN, no pause frames (half duplex) */
    wr(MACON3, 0x32);                       /* pad to 60 + CRC, TXCRCEN, FRMLNEN, half duplex */
    wr(MACON4, 0x40);                       /* DEFER: wait for a free medium */
    wr16(MAMXFLL, FRAME_MAX);
    wr(MABBIPG, 0x12);                      /* half-duplex gaps (datasheet values) */
    wr(MAIPGL, 0x12);
    wr(MAIPGH, 0x0C);
    wr(MAADR1, mac[0]);
    wr(MAADR2, mac[1]);
    wr(MAADR3, mac[2]);
    wr(MAADR4, mac[3]);
    wr(MAADR5, mac[4]);
    wr(MAADR6, mac[5]);
    phy_write(PHCON1, 0);                   /* half duplex, like MACON3 */
    phy_write(PHCON2, PHCON2_HDLDIS);       /* no loopback of our own frames */
    op(OP_BFS, ECON2, ECON2_AUTOINC);
    op(OP_BFS, ECON1, ECON1_RXEN);
    link_up = false;
    link_at = umcub_port_millis() - 1000u;
    return UMCUB_OK;
}

void umcub_port_eth_deinit(void)
{
    if (READ_BIT(RCC->APB2ENR, RCC_APB2ENR_SPI1EN)) {
        cs(true);
        (void)xfer(OP_SRC);                 /* the chip stops receiving */
        spi_end();
    }
    SET_BIT(RCC->APB2RSTR, RCC_APB2RSTR_SPI1RST);
    CLEAR_BIT(RCC->APB2RSTR, RCC_APB2RSTR_SPI1RST);
    CLEAR_BIT(RCC->APB2ENR, RCC_APB2ENR_SPI1EN);
    umcub_port_gpio_reset(ENC28J60_SCK_PIN);
    umcub_port_gpio_reset(ENC28J60_MISO_PIN);
    umcub_port_gpio_reset(ENC28J60_MOSI_PIN);
    umcub_port_gpio_reset(ENC28J60_CS_PIN);
#ifdef ENC28J60_RESET_PIN
    umcub_port_gpio_reset(ENC28J60_RESET_PIN);
#endif
}

bool umcub_port_eth_link(void)
{
    if (umcub_port_millis() - link_at >= 200u) {
        link_at = umcub_port_millis();
        link_up = (phy_read(PHSTAT2) & PHSTAT2_LSTAT) != 0;
    }
    return link_up;
}

int umcub_port_eth_tx(const uint8_t *frame, size_t len)
{
    if (len > FRAME_MAX - 4u) {
        return UMCUB_EINVAL;
    }
    op(OP_BFS, ECON1, ECON1_TXRST);         /* errata: reset the transmit logic first */
    op(OP_BFC, ECON1, ECON1_TXRST);
    op(OP_BFC, EIR, EIR_TXIF | EIR_TXERIF);
    wr16(EWRPTL, TX_START);
    static const uint8_t control = 0;       /* per-packet control: MACON3 settings */
    buf_write(&control, 1);
    buf_write(frame, len);
    wr16(ETXNDL, (uint16_t)(TX_START + len));
    op(OP_BFS, ECON1, ECON1_TXRTS);
    for (uint32_t t0 = umcub_port_millis(); rd(ECON1) & ECON1_TXRTS;) {
        if (umcub_port_millis() - t0 > 20u) {      /* 1.2 ms for 1518 bytes + deferral */
            op(OP_BFC, ECON1, ECON1_TXRTS);
            return UMCUB_ETIMEOUT;
        }
    }
    return (rd(EIR) & EIR_TXERIF) ? UMCUB_EIO : UMCUB_OK;
}

size_t umcub_port_eth_rx(uint8_t *buf, size_t max)
{
    if (rd(EPKTCNT) == 0) {
        return 0;
    }
    uint8_t hdr[6];                         /* next packet, byte count, status */
    wr16(ERDPTL, next_packet);
    buf_read(hdr, sizeof(hdr));
    next_packet = (uint16_t)(hdr[0] | hdr[1] << 8);
    if (next_packet > RX_END || (next_packet & 1u)) {     /* the ring is corrupt: start it over */
        op(OP_BFC, ECON1, ECON1_RXEN);
        op(OP_BFS, ECON1, ECON1_RXRST);
        op(OP_BFC, ECON1, ECON1_RXRST);
        next_packet = RX_START;
        wr16(ERXSTL, RX_START);
        wr16(ERXRDPTL, RX_END);
        op(OP_BFS, ECON1, ECON1_RXEN);
        return 0;
    }
    size_t n = (size_t)(hdr[2] | hdr[3] << 8);
    bool ok = (hdr[4] & 0x80u) && n >= 4u && n - 4u <= max;   /* RXOK; byte count with the CRC */
    n = ok ? n - 4u : 0;
    if (n) {
        buf_read(buf, n);
    }
    uint16_t rdpt = next_packet == RX_START ? RX_END : (uint16_t)(next_packet - 1u);     /* odd */
    wr16(ERXRDPTL, rdpt);
    op(OP_BFS, ECON2, ECON2_PKTDEC);
    return n;
}

#endif
