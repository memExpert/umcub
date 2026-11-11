/*
 * Private header of the STM32G4 port.
 */
#ifndef UMCUB_PORT_G4_H
#define UMCUB_PORT_G4_H

#include "umcub_cfg.h"
#include "umcub_port.h"
#include "stm32g4xx.h"
#include "../arch/cortexm/cortexm_port.h"
#include "../arch/stm32/stm32_common.h"

/* Clock frequencies after umcub_port_init() (filled in by clock.c). */
extern uint32_t g4_sysclk_hz;
extern uint32_t g4_pclk1_hz;    /* APB1: USART2/3, FDCAN kernel clock */
extern uint32_t g4_pclk2_hz;    /* APB2: USART1 */

void g4_clock_init(void);
void g4_clock_deinit(void);

GPIO_TypeDef *g4_gpio_port(uint32_t pin);

#endif /* UMCUB_PORT_G4_H */
