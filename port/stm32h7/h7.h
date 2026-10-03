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

/* Peripheral reset bookkeeping: drivers register the RCC reset bit of every
 * peripheral they enable so deinit can put it back into reset state. */
void h7_periph_used(volatile uint32_t *rstr, uint32_t mask);

GPIO_TypeDef *h7_gpio_port(uint32_t pin);

/* Busy-wait until (*reg & mask) == value or timeout. */
int h7_wait(volatile uint32_t *reg, uint32_t mask, uint32_t value, uint32_t timeout_ms);

#endif /* UMCUB_PORT_H7_H */
