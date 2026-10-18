/*
 * USART drivers shared by the family ports (umcub_port_uart_*):
 *  - usart_v2.c: ISR / ICR / RDR / TDR (H7, G4, L4, L5, F7, F0, F3, ...);
 *  - usart_v1.c: SR / DR (F1, F2, F4, L1).
 * The family supplies its instances and pins (stm32_usart_get()) and the
 * vectors, which call stm32_usart_isr().
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

#include "umcub_port_uart.h"

typedef struct {
    USART_TypeDef *regs;
    volatile uint32_t *enr;     /* RCC clock enable register */
    volatile uint32_t *rstr;    /* RCC reset register (same bit) */
    uint32_t bit;
    const uint32_t *clock_hz;   /* kernel clock of the instance (set by the clock setup) */
    IRQn_Type irq;
} stm32_usart_t;

/* Family: the instance of `cfg` with its TX / RX pins set up (alternate
 * function / remap); NULL if the part has no such instance or these are not
 * its pins. */
const stm32_usart_t *stm32_usart_get(const umcub_uart_cfg_t *cfg);

/* The interrupt of the active instance; every family vector calls it. */
void stm32_usart_isr(void);

#endif
