/*
 * STM32F1 port: USART1..3 (RM0008 §27), RX interrupt into a ring buffer,
 * polled TX. Pins: the default mapping or the AFIO remap the TX pin selects
 * (RM0008 §9.3.8): USART1 PA9/PA10 or PB6/PB7, USART2 PA2/PA3 or PD5/PD6,
 * USART3 PB10/PB11, PC10/PC11 (partial) or PD8/PD9 (full). Remapped pins
 * exist only on bigger packages (RM0008 Tables 52/53).
 */
#include "f1.h"
#include "umcub_port_uart.h"

#define RX_RING 512u   /* power of two */

static USART_TypeDef *uart;
static IRQn_Type uart_irq;
static uint32_t uart_pins[2];
static uint32_t de_pin = UMCUB_PIN_NONE;   /* RS485 DE, always software on F1 (no hardware DE) */
static bool de_level;
static uint8_t rx_buf[RX_RING];
static stm32_ring_t rx;

#define PIN(port, n) UMCUB_PIN(port, n, 0)
#define SAME_PIN(a, b) (((a) & ~0xFu) == ((b) & ~0xFu))   /* ignore the AF field */

struct uart_map {
    unsigned instance;
    uint32_t tx, rx;
    uint32_t remap_mask, remap_val;   /* AFIO_MAPR */
};

static const struct uart_map maps[] = {
    { 1, PIN('A', 9), PIN('A', 10), AFIO_MAPR_USART1_REMAP, 0 },
    { 1, PIN('B', 6), PIN('B', 7), AFIO_MAPR_USART1_REMAP, AFIO_MAPR_USART1_REMAP },
    { 2, PIN('A', 2), PIN('A', 3), AFIO_MAPR_USART2_REMAP, 0 },
    { 2, PIN('D', 5), PIN('D', 6), AFIO_MAPR_USART2_REMAP, AFIO_MAPR_USART2_REMAP },
    { 3, PIN('B', 10), PIN('B', 11), AFIO_MAPR_USART3_REMAP, 0 },
    { 3, PIN('C', 10), PIN('C', 11), AFIO_MAPR_USART3_REMAP, AFIO_MAPR_USART3_REMAP_PARTIALREMAP },
    { 3, PIN('D', 8), PIN('D', 9), AFIO_MAPR_USART3_REMAP, AFIO_MAPR_USART3_REMAP_FULLREMAP },
};

static void uart_isr(void)
{
    uint32_t sr = uart->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
        uint8_t c = (uint8_t)uart->DR;  /* SR then DR read clears the error flags too */
        if (sr & USART_SR_RXNE) {
            stm32_ring_put(&rx, rx_buf, RX_RING, c);
        }
    }
}

void USART1_IRQHandler(void) { uart_isr(); }
void USART2_IRQHandler(void) { uart_isr(); }
void USART3_IRQHandler(void) { uart_isr(); }

int umcub_port_uart_init(const umcub_uart_cfg_t *cfg)
{
    unsigned instance = cfg->instance;
    uint32_t baud = cfg->baud, tx_pin = cfg->tx_pin, rx_pin = cfg->rx_pin;
    const struct uart_map *m = NULL;
    for (unsigned i = 0; i < sizeof(maps) / sizeof(maps[0]); i++) {
        if (maps[i].instance == instance && SAME_PIN(maps[i].tx, tx_pin) && SAME_PIN(maps[i].rx, rx_pin)) {
            m = &maps[i];
        }
    }
    if (!m || baud == 0) {
        return UMCUB_EINVAL;
    }

    uint32_t pclk;
    switch (instance) {
    case 1:
        uart = USART1;
        uart_irq = USART1_IRQn;
        umcub_cm_periph_on(&RCC->APB2ENR, &RCC->APB2RSTR, RCC_APB2ENR_USART1EN);
        pclk = f1_pclk2_hz;
        break;
    case 2:
        uart = USART2;
        uart_irq = USART2_IRQn;
        umcub_cm_periph_on(&RCC->APB1ENR, &RCC->APB1RSTR, RCC_APB1ENR_USART2EN);
        pclk = f1_pclk1_hz;
        break;
    default:
        uart = USART3;
        uart_irq = USART3_IRQn;
        umcub_cm_periph_on(&RCC->APB1ENR, &RCC->APB1RSTR, RCC_APB1ENR_USART3EN);
        pclk = f1_pclk1_hz;
        break;
    }
    if (m->remap_val) {
        umcub_cm_periph_on(&RCC->APB2ENR, &RCC->APB2RSTR, RCC_APB2ENR_AFIOEN);
        /* SWJ_CFG is write-only and reads back undefined (RM0008 §9.4.2): write
         * 111 ("no effect") with the remap bits, or SWD may get switched off. */
        MODIFY_REG(AFIO->MAPR, m->remap_mask | AFIO_MAPR_SWJ_CFG, m->remap_val | AFIO_MAPR_SWJ_CFG);
    }
    uart_pins[0] = tx_pin;
    uart_pins[1] = rx_pin;
    rx.head = rx.tail = 0;

    f1_gpio_config(tx_pin, F1_GPIO_AF_PP_50M);
    umcub_port_gpio_input(rx_pin, UMCUB_PULL_UP);   /* idle high if unconnected */
    de_pin = cfg->de_pin;
    de_level = cfg->de_active_high;
    if (de_pin != UMCUB_PIN_NONE) {
        umcub_port_gpio_output(de_pin, !de_level);  /* receive */
    }

    uart->CR1 = 0;
    uart->CR2 = 0;
    uart->CR3 = 0;
    uart->BRR = (pclk + baud / 2u) / baud;           /* 16x oversampling: mantissa.fraction */
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
