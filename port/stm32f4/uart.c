/*
 * STM32F4 port: USART instances for the USART v1 driver
 * (port/arch/stm32/usart_v1.c, RM0383 §19). F411: USART1, USART2, USART6;
 * the pins and their AF numbers come from the board config (DS10314 Table 9).
 */
#include "f4.h"
#include "../arch/stm32/stm32_usart.h"

#define APB1(n, irq) { n, &RCC->APB1ENR, &RCC->APB1RSTR, RCC_APB1ENR_##n##EN, &f4_pclk1_hz, irq }
#define APB2(n, irq) { n, &RCC->APB2ENR, &RCC->APB2RSTR, RCC_APB2ENR_##n##EN, &f4_pclk2_hz, irq }

static const stm32_usart_t usarts[] = {
    APB2(USART1, USART1_IRQn), APB1(USART2, USART2_IRQn), { 0 }, { 0 }, { 0 }, APB2(USART6, USART6_IRQn),
};

const stm32_usart_t *stm32_usart_get(const umcub_uart_cfg_t *cfg)
{
    if (cfg->instance - 1u >= sizeof(usarts) / sizeof(usarts[0]) || !usarts[cfg->instance - 1u].regs) {
        return NULL;
    }
    umcub_port_gpio_af(cfg->tx_pin);
    umcub_port_gpio_af(cfg->rx_pin);
    f4_gpio_pull_up(cfg->rx_pin);
    return &usarts[cfg->instance - 1u];
}

void USART1_IRQHandler(void) { stm32_usart_isr(); }
void USART2_IRQHandler(void) { stm32_usart_isr(); }
void USART6_IRQHandler(void) { stm32_usart_isr(); }
