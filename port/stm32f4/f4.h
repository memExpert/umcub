/*
 * Private header of the STM32F4 port.
 */
#ifndef UMCUB_PORT_F4_H
#define UMCUB_PORT_F4_H

#include "umcub_cfg.h"
#include "umcub_port.h"
#include "stm32f4xx.h"
#include "../arch/cortexm/cortexm_port.h"
#include "../arch/stm32/stm32_common.h"

/* Clock frequencies after umcub_port_init() (filled in by clock.c). */
extern uint32_t f4_sysclk_hz;
extern uint32_t f4_pclk1_hz;    /* APB1: USART2 */
extern uint32_t f4_pclk2_hz;    /* APB2: USART1, USART6 */

void f4_clock_init(void);
void f4_clock_deinit(void);

/* Pull-up on a pin in its current mode (UART RX: idle high when unconnected). */
void f4_gpio_pull_up(uint32_t pin);

#endif /* UMCUB_PORT_F4_H */
