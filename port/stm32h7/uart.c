/*
 * STM32H7 port: USART/UART with LL (RM0399 §48).
 */
#include "h7.h"
#include "umcub_port_uart.h"
#include "stm32h7xx_ll_usart.h"

#define RX_RING 1024u   /* power of two */

static USART_TypeDef *uart;
static IRQn_Type uart_irq;
static uint32_t uart_pins[2];
static uint8_t rx_ring[RX_RING];
static volatile uint32_t rx_head;
static uint32_t rx_tail;

struct uart_desc {
    USART_TypeDef *regs;
    IRQn_Type irq;
    volatile uint32_t *enr;
    volatile uint32_t *rstr;
    uint32_t bit;
};

static bool lookup(unsigned instance, struct uart_desc *d)
{
    switch (instance) {
    case 1: *d = (struct uart_desc){ USART1, USART1_IRQn, &RCC->APB2ENR, &RCC->APB2RSTR, RCC_APB2ENR_USART1EN }; return true;
    case 2: *d = (struct uart_desc){ USART2, USART2_IRQn, &RCC->APB1LENR, &RCC->APB1LRSTR, RCC_APB1LENR_USART2EN }; return true;
    case 3: *d = (struct uart_desc){ USART3, USART3_IRQn, &RCC->APB1LENR, &RCC->APB1LRSTR, RCC_APB1LENR_USART3EN }; return true;
    case 4: *d = (struct uart_desc){ UART4, UART4_IRQn, &RCC->APB1LENR, &RCC->APB1LRSTR, RCC_APB1LENR_UART4EN }; return true;
    case 5: *d = (struct uart_desc){ UART5, UART5_IRQn, &RCC->APB1LENR, &RCC->APB1LRSTR, RCC_APB1LENR_UART5EN }; return true;
    case 6: *d = (struct uart_desc){ USART6, USART6_IRQn, &RCC->APB2ENR, &RCC->APB2RSTR, RCC_APB2ENR_USART6EN }; return true;
    case 7: *d = (struct uart_desc){ UART7, UART7_IRQn, &RCC->APB1LENR, &RCC->APB1LRSTR, RCC_APB1LENR_UART7EN }; return true;
    case 8: *d = (struct uart_desc){ UART8, UART8_IRQn, &RCC->APB1LENR, &RCC->APB1LRSTR, RCC_APB1LENR_UART8EN }; return true;
    default: return false;
    }
}

static void uart_isr(void)
{
    uint32_t isr = uart->ISR;
    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) {
        uart->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    }
    if (isr & USART_ISR_RXNE_RXFNE) {
        uint8_t c = (uint8_t)uart->RDR;
        uint32_t h = rx_head;
        if ((uint32_t)(h - rx_tail) < RX_RING) {
            rx_ring[h & (RX_RING - 1u)] = c;
            __DMB();            /* data before index */
            rx_head = h + 1u;
        }
    }
}

void USART1_IRQHandler(void) { uart_isr(); }
void USART2_IRQHandler(void) { uart_isr(); }
void USART3_IRQHandler(void) { uart_isr(); }
void UART4_IRQHandler(void) { uart_isr(); }
void UART5_IRQHandler(void) { uart_isr(); }
void USART6_IRQHandler(void) { uart_isr(); }
void UART7_IRQHandler(void) { uart_isr(); }
void UART8_IRQHandler(void) { uart_isr(); }

int umcub_port_uart_init(unsigned instance, uint32_t baud, uint32_t tx_pin, uint32_t rx_pin)
{
    struct uart_desc d;
    if (!lookup(instance, &d)) {
        return UMCUB_EINVAL;
    }
    uart = d.regs;
    uart_irq = d.irq;
    uart_pins[0] = tx_pin;
    uart_pins[1] = rx_pin;
    rx_head = rx_tail = 0;

    SET_BIT(*d.enr, d.bit);
    (void)*d.enr;
    h7_periph_used(d.rstr, d.bit);

    umcub_port_gpio_af(tx_pin);
    umcub_port_gpio_af(rx_pin);

    LL_USART_Disable(uart);
    LL_USART_SetPrescaler(uart, LL_USART_PRESCALER_DIV1);
    LL_USART_SetBaudRate(uart, h7_pclk_hz, LL_USART_PRESCALER_DIV1, LL_USART_OVERSAMPLING_16, baud);
    LL_USART_ConfigCharacter(uart, LL_USART_DATAWIDTH_8B, LL_USART_PARITY_NONE, LL_USART_STOPBITS_1);
    LL_USART_SetTransferDirection(uart, LL_USART_DIRECTION_TX_RX);
    LL_USART_SetOverSampling(uart, LL_USART_OVERSAMPLING_16);
    LL_USART_EnableIT_RXNE_RXFNE(uart);
    LL_USART_Enable(uart);

    NVIC_SetPriority(uart_irq, 2);
    NVIC_EnableIRQ(uart_irq);
    return UMCUB_OK;
}

void umcub_port_uart_deinit(void)
{
    if (!uart) {
        return;
    }
    (void)h7_wait(&uart->ISR, USART_ISR_TC, USART_ISR_TC, 50);
    NVIC_DisableIRQ(uart_irq);
    LL_USART_Disable(uart);
    umcub_port_gpio_reset(uart_pins[0]);
    umcub_port_gpio_reset(uart_pins[1]);
    uart = NULL;
}

size_t umcub_port_uart_read(uint8_t *buf, size_t max)
{
    size_t n = 0;
    uint32_t head = rx_head;
    __DMB();
    while (n < max && rx_tail != head) {
        buf[n++] = rx_ring[rx_tail & (RX_RING - 1u)];
        rx_tail++;
    }
    return n;
}

void umcub_port_uart_write(const uint8_t *buf, size_t len)
{
    if (!uart) {
        return;
    }
    while (len--) {
        while (!LL_USART_IsActiveFlag_TXE_TXFNF(uart)) {
        }
        uart->TDR = *buf++;
    }
}
