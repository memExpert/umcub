/*
 * Private header of the STM32F1 port.
 */
#ifndef UMCUB_PORT_F1_H
#define UMCUB_PORT_F1_H

#include "umcub_cfg.h"
#include "umcub_port.h"
#include "stm32f1xx.h"

/* Clock frequencies after umcub_port_init() (filled in by clock.c). */
extern uint32_t f1_sysclk_hz;
extern uint32_t f1_pclk1_hz;    /* APB1: USART2/3 */
extern uint32_t f1_pclk2_hz;    /* APB2: USART1 */

void f1_clock_init(void);
void f1_clock_deinit(void);

/* Peripheral reset bookkeeping: drivers register the RCC reset bit of every
 * peripheral they enable so deinit can put it back into reset state. */
void f1_periph_used(volatile uint32_t *rstr, uint32_t mask);

GPIO_TypeDef *f1_gpio_port(uint32_t pin);
/* Raw 4-bit CNF|MODE configuration of one pin (RM0008 §9.2.1/9.2.2). */
void f1_gpio_config(uint32_t pin, uint32_t cnf_mode);
#define F1_GPIO_ANALOG      0x0u
#define F1_GPIO_IN_FLOAT    0x4u    /* reset state */
#define F1_GPIO_IN_PULL     0x8u    /* pull direction from ODR */
#define F1_GPIO_AF_PP_50M   0xBu

/* Busy-wait until (*reg & mask) == value or timeout. */
int f1_wait(volatile uint32_t *reg, uint32_t mask, uint32_t value, uint32_t timeout_ms);

#endif /* UMCUB_PORT_F1_H */
