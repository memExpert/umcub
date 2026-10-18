/*
 * bxCAN driver shared by the family ports (port/arch/stm32/bxcan.c, classic
 * CAN: F1, F2, F4, F7, L4, F0, F3). The family supplies the instance with its
 * pins set up (stm32_bxcan_get()). CAN1 only: a second controller (CAN2 on
 * F105/F107, F4) takes its filters from CAN1's banks, not handled yet.
 */
#ifndef UMCUB_STM32_CAN_H
#define UMCUB_STM32_CAN_H

#include <stdint.h>
#include "umcub_family_cmsis.h"
#include "umcub_port_can.h"

typedef struct {
    CAN_TypeDef *regs;
    volatile uint32_t *enr;     /* RCC clock enable register */
    volatile uint32_t *rstr;    /* RCC reset register (same bit) */
    uint32_t bit;
    const uint32_t *clock_hz;   /* APB clock of the controller (set by the clock setup) */
} stm32_bxcan_t;

/* Family: the controller of `cfg` with its TX / RX pins set up (alternate
 * function / remap); NULL if the part has no such instance or these are not
 * its pins. */
const stm32_bxcan_t *stm32_bxcan_get(const umcub_can_cfg_t *cfg);

#endif
