/*
 * USART v2 (ISR / ICR / RDR / TDR: H7, G4, L4, L5, F7, F0, F3, ...) shared by
 * the family ports: port/arch/stm32/usart_v2.c implements umcub_port_uart_*;
 * the family supplies its instances (stm32_usart_find()) and the vectors,
 * which call stm32_usart_isr().
 */
#ifndef UMCUB_STM32_USART_H
#define UMCUB_STM32_USART_H

#include <stdbool.h>
#include <stdint.h>
#include "umcub_family_cmsis.h"

/* Parts without the USART FIFO name the same bits without the FIFO part. */
#ifndef USART_ISR_RXNE_RXFNE
#define USART_ISR_RXNE_RXFNE    USART_ISR_RXNE
#define USART_ISR_TXE_TXFNF     USART_ISR_TXE
#define USART_CR1_RXNEIE_RXFNEIE USART_CR1_RXNEIE
#define USART_CR1_TXEIE_TXFNFIE USART_CR1_TXEIE
#endif

typedef struct {
    USART_TypeDef *regs;
    IRQn_Type irq;
    volatile uint32_t *enr;     /* RCC clock enable register */
    volatile uint32_t *rstr;    /* RCC reset register (same bit) */
    uint32_t bit;
    uint32_t clock_hz;          /* kernel clock of the instance */
} stm32_usart_t;

/* Family: USART / UART `instance` (1 = USART1); false if the part has none. */
bool stm32_usart_find(unsigned instance, stm32_usart_t *u);

/* The interrupt of the active instance; every family vector calls it. */
void stm32_usart_isr(void);

#endif
