/*
 * STM32H7 port: USART / UART instances for the USART v2 driver
 * (port/arch/stm32/usart_v2.c, RM0399 §48).
 */
#include "h7.h"
#include "../arch/stm32/stm32_usart.h"

static const struct {
    USART_TypeDef *regs;
    uint32_t bit;               /* RCC enable / reset bit */
    int8_t irq;
    bool apb2;                  /* APB2ENR, else APB1LENR */
} usarts[] = {
    { USART1, RCC_APB2ENR_USART1EN, USART1_IRQn, true },
    { USART2, RCC_APB1LENR_USART2EN, USART2_IRQn, false },
    { USART3, RCC_APB1LENR_USART3EN, USART3_IRQn, false },
    { UART4, RCC_APB1LENR_UART4EN, UART4_IRQn, false },
    { UART5, RCC_APB1LENR_UART5EN, UART5_IRQn, false },
    { USART6, RCC_APB2ENR_USART6EN, USART6_IRQn, true },
    { UART7, RCC_APB1LENR_UART7EN, UART7_IRQn, false },
    { UART8, RCC_APB1LENR_UART8EN, UART8_IRQn, false },
};

bool stm32_usart_find(unsigned instance, stm32_usart_t *u)
{
    if (instance - 1u >= sizeof(usarts) / sizeof(usarts[0])) {
        return false;
    }
    const __typeof__(usarts[0]) *e = &usarts[instance - 1u];
    u->regs = e->regs;
    u->irq = (IRQn_Type)e->irq;
    u->enr = e->apb2 ? &RCC->APB2ENR : &RCC->APB1LENR;
    u->rstr = e->apb2 ? &RCC->APB2RSTR : &RCC->APB1LRSTR;
    u->bit = e->bit;
    u->clock_hz = h7_pclk_hz;       /* every kernel clock left on its PCLK (reset default) */
    return true;
}

void USART1_IRQHandler(void) { stm32_usart_isr(); }
void USART2_IRQHandler(void) { stm32_usart_isr(); }
void USART3_IRQHandler(void) { stm32_usart_isr(); }
void UART4_IRQHandler(void) { stm32_usart_isr(); }
void UART5_IRQHandler(void) { stm32_usart_isr(); }
void USART6_IRQHandler(void) { stm32_usart_isr(); }
void UART7_IRQHandler(void) { stm32_usart_isr(); }
void UART8_IRQHandler(void) { stm32_usart_isr(); }
