/*
 * STM32G4 port: USART instances for the ISR / ICR USART driver
 * (port/arch/stm32/usart_isr.c, RM0440 §37). Kernel clocks left on PCLK
 * (USARTxSEL = 00, the reset default).
 */
#include "g4.h"
#include "stm32g4xx_ll_gpio.h"
#include "../arch/stm32/stm32_usart.h"

static const stm32_usart_t usarts[] = {
    { USART1, &RCC->APB2ENR, &RCC->APB2RSTR, RCC_APB2ENR_USART1EN, &g4_pclk2_hz, USART1_IRQn },
    { USART2, &RCC->APB1ENR1, &RCC->APB1RSTR1, RCC_APB1ENR1_USART2EN, &g4_pclk1_hz, USART2_IRQn },
    { USART3, &RCC->APB1ENR1, &RCC->APB1RSTR1, RCC_APB1ENR1_USART3EN, &g4_pclk1_hz, USART3_IRQn },
};

const stm32_usart_t *stm32_usart_get(const umcub_uart_cfg_t *cfg)
{
    if (cfg->instance - 1u >= sizeof(usarts) / sizeof(usarts[0])) {
        return NULL;
    }
    umcub_port_gpio_af(cfg->tx_pin);
    umcub_port_gpio_af(cfg->rx_pin);
    LL_GPIO_SetPinPull(g4_gpio_port(cfg->rx_pin), 1u << UMCUB_PIN_NUM(cfg->rx_pin), LL_GPIO_PULL_UP);   /* idle high if unconnected */
    return &usarts[cfg->instance - 1u];
}

void USART1_IRQHandler(void) { stm32_usart_isr(); }
void USART2_IRQHandler(void) { stm32_usart_isr(); }
void USART3_IRQHandler(void) { stm32_usart_isr(); }
