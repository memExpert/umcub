/*
 * Private header of the STM32H7 port.
 */
#ifndef UMCUB_PORT_H7_H
#define UMCUB_PORT_H7_H

#include "umcub_cfg.h"
#include "umcub_port.h"
#include "stm32h7xx.h"
#include "stm32h7xx_ll_bus.h"
#include "stm32h7xx_ll_gpio.h"
#include "stm32h7xx_ll_pwr.h"
#include "stm32h7xx_ll_rcc.h"
#include "stm32h7xx_ll_system.h"
#include "../common/cortexm_port.h"

#if defined(CORE_CM4)
#define H7_IS_CM4 1
#else
#define H7_IS_CM4 0
#endif

/* Clock frequencies after umcub_port_init() (filled in by clock.c). */
extern uint32_t h7_sysclk_hz;   /* CPU clock of the running core */
extern uint32_t h7_hclk_hz;     /* AHB / AXI */
extern uint32_t h7_pclk_hz;     /* all APB buses (same divider) */
extern uint32_t h7_pll1q_hz;    /* FDCAN kernel clock */

void h7_clock_init(void);
void h7_clock_deinit(void);
/* HSI48 on (kernel clock of USB and RNG); UMCUB_EIO if it does not start. */
int h7_hsi48_on(void);

GPIO_TypeDef *h7_gpio_port(uint32_t pin);

#endif /* UMCUB_PORT_H7_H */
