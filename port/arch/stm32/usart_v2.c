/*
 * USART v2 driver (umcub_port_uart_*), register level, for every family
 * with the ISR / ICR / RDR / TDR register set; the instance comes from the
 * family (stm32_usart_find()). RS485: hardware DE when the DE pin has the
 * USART's RTS/DE alternate function, otherwise a GPIO switched by software.
 */
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "umcub_port_uart.h"
#include "stm32_usart.h"
#include "stm32_common.h"

#define RX_RING 1024u   /* power of two */

static USART_TypeDef *uart;
static IRQn_Type uart_irq;
static uint32_t uart_pins[2];
static uint32_t de_pin = UMCUB_PIN_NONE;
static bool de_hw, de_level;
static uint8_t rx_buf[RX_RING];
static stm32_ring_t rx;
#if UMCUB_CFG_UART_TX_RING
static uint8_t tx_buf[UMCUB_CFG_UART_TX_RING];
static stm32_ring_t tx;
#endif

void stm32_usart_isr(void)
{
    uint32_t isr = uart->ISR;
    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) {
        uart->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    }
    if (isr & USART_ISR_RXNE_RXFNE) {
        (void)stm32_ring_put(&rx, rx_buf, RX_RING, (uint8_t)uart->RDR);
    }
#if UMCUB_CFG_UART_TX_RING
    if ((isr & USART_ISR_TXE_TXFNF) && (uart->CR1 & USART_CR1_TXEIE_TXFNFIE)) {
        uint8_t c;
        if (stm32_ring_pop(&tx, tx_buf, UMCUB_CFG_UART_TX_RING, &c)) {
            uart->TDR = c;
        } else {
            CLEAR_BIT(uart->CR1, USART_CR1_TXEIE_TXFNFIE);
        }
    }
#endif
}

int umcub_port_uart_init(const umcub_uart_cfg_t *cfg)
{
    stm32_usart_t u;
    if (!stm32_usart_find(cfg->instance, &u) || cfg->baud == 0) {
        return UMCUB_EINVAL;
    }
    uart = u.regs;
    uart_irq = u.irq;
    uart_pins[0] = cfg->tx_pin;
    uart_pins[1] = cfg->rx_pin;
    rx.head = rx.tail = 0;
#if UMCUB_CFG_UART_TX_RING
    tx.head = tx.tail = 0;
#endif
    de_pin = cfg->de_pin;
    de_level = cfg->de_active_high;
    /* RTS/DE alternate function given: the USART drives DE itself (USART
     * "RS485 driver enable"); AF 0: software switches a GPIO. */
    de_hw = de_pin != UMCUB_PIN_NONE && UMCUB_PIN_AF(de_pin) != 0;

    umcub_cm_periph_on(u.enr, u.rstr, u.bit);

    umcub_port_gpio_af(cfg->tx_pin);
    umcub_port_gpio_af(cfg->rx_pin);
    if (de_hw) {
        /* The RTS/DE pin level between AF selection and UE = 1 is not
         * specified; on a shared bus a transceiver pull-down on DE keeps it
         * passive meanwhile. */
        umcub_port_gpio_af(de_pin);
    } else if (de_pin != UMCUB_PIN_NONE) {
        umcub_port_gpio_output(de_pin, !de_level);      /* receive */
    }

    /* 8N1, oversampling by 16, no prescaler. */
    uart->CR1 = 0;
#ifdef USART_PRESC_PRESCALER
    uart->PRESC = 0;
#endif
    uart->BRR = (u.clock_hz + cfg->baud / 2u) / cfg->baud;
    uart->CR2 = 0;
    uart->CR3 = 0;
    uint32_t cr1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE;
    if (de_hw) {
        /* DE asserted/released half a bit time around the frame (in 1/16 bit). */
        cr1 |= (8u << USART_CR1_DEAT_Pos) | (8u << USART_CR1_DEDT_Pos);
        uart->CR3 = USART_CR3_DEM | (de_level ? 0 : USART_CR3_DEP);
    }
    uart->CR1 = cr1;                                    /* DEAT / DEDT only while UE = 0 */
    uart->CR1 = cr1 | USART_CR1_UE;

    NVIC_SetPriority(uart_irq, 2);
    NVIC_EnableIRQ(uart_irq);
    return UMCUB_OK;
}

void umcub_port_uart_deinit(void)
{
    if (!uart) {
        return;
    }
#if UMCUB_CFG_UART_TX_RING
    stm32_ring_drain(&tx, 500);
#endif
    (void)umcub_cm_wait(&uart->ISR, USART_ISR_TC, USART_ISR_TC, 50);
    NVIC_DisableIRQ(uart_irq);
    uart->CR1 = 0;
    umcub_port_gpio_reset(uart_pins[0]);
    umcub_port_gpio_reset(uart_pins[1]);
    if (de_pin != UMCUB_PIN_NONE) {
        umcub_port_gpio_reset(de_pin);
        de_pin = UMCUB_PIN_NONE;
    }
    uart = NULL;
}

size_t umcub_port_uart_read(uint8_t *buf, size_t max)
{
    return stm32_ring_get(&rx, rx_buf, RX_RING, buf, max);
}

void umcub_port_uart_write(const uint8_t *buf, size_t len)
{
    if (!uart || !len) {
        return;
    }
#if UMCUB_CFG_UART_TX_RING
    stm32_ring_write(&tx, tx_buf, UMCUB_CFG_UART_TX_RING, buf, len, &uart->CR1, USART_CR1_TXEIE_TXFNFIE);
    return;
#endif
    bool rs485 = de_pin != UMCUB_PIN_NONE;
    if (rs485) {
        CLEAR_BIT(uart->CR1, USART_CR1_RE);             /* no echo of our own bytes */
        if (!de_hw) {
            umcub_port_gpio_write(de_pin, de_level);
        }
    }
    while (len--) {
        if (umcub_cm_wait(&uart->ISR, USART_ISR_TXE_TXFNF, USART_ISR_TXE_TXFNF, 10) != 0) {
            break;                                  /* transmitter stuck: drop the rest */
        }
        uart->TDR = *buf++;
    }
    if (rs485) {
        (void)umcub_cm_wait(&uart->ISR, USART_ISR_TC, USART_ISR_TC, 10);   /* last stop bit out */
        if (!de_hw) {
            umcub_port_gpio_write(de_pin, !de_level);
        }
        SET_BIT(uart->CR1, USART_CR1_RE);
    }
}
