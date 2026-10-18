/*
 * USART v1 driver (umcub_port_uart_*), register level, for the families with
 * the SR / DR register set (F1, F2, F4, L1); the instance and its pins come
 * from the family (stm32_usart_find(), stm32_usart_pins()). RX interrupt into
 * a ring, polled TX (or the TX ring). RS485: DE always switched by software
 * (no hardware DE on this USART).
 */
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "umcub_port_uart.h"
#include "stm32_usart.h"
#include "stm32_common.h"

#define RX_RING 512u   /* power of two */

static USART_TypeDef *uart;
static IRQn_Type uart_irq;
static uint32_t uart_pins[2];
static uint32_t de_pin = UMCUB_PIN_NONE;
static bool de_level;
static uint8_t rx_buf[RX_RING];
static stm32_ring_t rx;
#if UMCUB_CFG_UART_TX_RING
static uint8_t tx_buf[UMCUB_CFG_UART_TX_RING];
static stm32_ring_t tx;
#endif

void stm32_usart_isr(void)
{
    uint32_t sr = uart->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
        uint8_t c = (uint8_t)uart->DR;  /* SR then DR read clears the error flags too */
        if (sr & USART_SR_RXNE) {
            (void)stm32_ring_put(&rx, rx_buf, RX_RING, c);
        }
    }
#if UMCUB_CFG_UART_TX_RING
    if ((sr & USART_SR_TXE) && (uart->CR1 & USART_CR1_TXEIE)) {
        uint8_t c;
        if (stm32_ring_pop(&tx, tx_buf, UMCUB_CFG_UART_TX_RING, &c)) {
            uart->DR = c;
        } else {
            CLEAR_BIT(uart->CR1, USART_CR1_TXEIE);
        }
    }
#endif
}

int umcub_port_uart_init(const umcub_uart_cfg_t *cfg)
{
    const stm32_usart_t *u = cfg->baud ? stm32_usart_get(cfg) : NULL;
    if (!u) {
        return UMCUB_EINVAL;
    }
    uart = u->regs;
    uart_irq = u->irq;
    umcub_cm_periph_on(u->enr, u->rstr, u->bit);
    uart_pins[0] = cfg->tx_pin;
    uart_pins[1] = cfg->rx_pin;
    rx.head = rx.tail = 0;
#if UMCUB_CFG_UART_TX_RING
    tx.head = tx.tail = 0;
#endif
    de_pin = cfg->de_pin;
    de_level = cfg->de_active_high;
    if (de_pin != UMCUB_PIN_NONE) {
        umcub_port_gpio_output(de_pin, !de_level);  /* receive */
    }

    uart->CR1 = 0;
    uart->CR2 = 0;
    uart->CR3 = 0;
    uart->BRR = (*u->clock_hz + cfg->baud / 2u) / cfg->baud;  /* 16x oversampling: mantissa.fraction */
    uart->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;

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
    (void)umcub_cm_wait(&uart->SR, USART_SR_TC, USART_SR_TC, 50);
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
    stm32_ring_write(&tx, tx_buf, UMCUB_CFG_UART_TX_RING, buf, len, &uart->CR1, USART_CR1_TXEIE);
    return;
#endif
    bool rs485 = de_pin != UMCUB_PIN_NONE;
    if (rs485) {
        CLEAR_BIT(uart->CR1, USART_CR1_RE);         /* no echo of our own bytes */
        umcub_port_gpio_write(de_pin, de_level);
    }
    while (len--) {
        if (umcub_cm_wait(&uart->SR, USART_SR_TXE, USART_SR_TXE, 10) != 0) {
            break;                                  /* transmitter stuck: drop the rest */
        }
        uart->DR = *buf++;
    }
    if (rs485) {
        (void)umcub_cm_wait(&uart->SR, USART_SR_TC, USART_SR_TC, 10);   /* last stop bit out */
        umcub_port_gpio_write(de_pin, !de_level);
        SET_BIT(uart->CR1, USART_CR1_RE);
    }
}
