/*
 * STM32H7 port: USART / UART instances for the USART v2 driver
 * (port/arch/stm32/usart_v2.c, RM0399 §48).
 */
#include "h7.h"
#include "../arch/stm32/stm32_usart.h"

#define APB1(n, irq) { n, &RCC->APB1LENR, &RCC->APB1LRSTR, RCC_APB1LENR_##n##EN, &h7_pclk_hz, irq }
#define APB2(n, irq) { n, &RCC->APB2ENR, &RCC->APB2RSTR, RCC_APB2ENR_##n##EN, &h7_pclk_hz, irq }

/* every kernel clock left on its PCLK (reset default) */
static const stm32_usart_t usarts[] = {
    APB2(USART1, USART1_IRQn), APB1(USART2, USART2_IRQn), APB1(USART3, USART3_IRQn), APB1(UART4, UART4_IRQn),
    APB1(UART5, UART5_IRQn), APB2(USART6, USART6_IRQn), APB1(UART7, UART7_IRQn), APB1(UART8, UART8_IRQn),
};

const stm32_usart_t *stm32_usart_get(const umcub_uart_cfg_t *cfg)
{
    if (cfg->instance - 1u >= sizeof(usarts) / sizeof(usarts[0])) {
        return NULL;
    }
    umcub_port_gpio_af(cfg->tx_pin);
    umcub_port_gpio_af(cfg->rx_pin);
    return &usarts[cfg->instance - 1u];
}

void USART1_IRQHandler(void) { stm32_usart_isr(); }
void USART2_IRQHandler(void) { stm32_usart_isr(); }
void USART3_IRQHandler(void) { stm32_usart_isr(); }
void UART4_IRQHandler(void) { stm32_usart_isr(); }
void UART5_IRQHandler(void) { stm32_usart_isr(); }
void USART6_IRQHandler(void) { stm32_usart_isr(); }
void UART7_IRQHandler(void) { stm32_usart_isr(); }
void UART8_IRQHandler(void) { stm32_usart_isr(); }
