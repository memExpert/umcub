/*
 * FDCAN (Bosch M_CAN) driver shared by the family ports
 * (port/arch/stm32/fdcan.c: H7, G4, H5, U5, L5). The core and the message
 * RAM element format are the same everywhere; the RAM layout differs (H7:
 * sections set by SIDFC / RXF0C / TXBC ..., G4: fixed) and is chosen in the
 * driver from the device header. The family supplies the controller with its
 * clock and pins (stm32_fdcan_get()).
 */
#ifndef UMCUB_STM32_FDCAN_H
#define UMCUB_STM32_FDCAN_H

#include <stdint.h>
#include "umcub_family_cmsis.h"
#include "umcub_port_can.h"

typedef struct {
    FDCAN_GlobalTypeDef *regs;
    volatile uint32_t *enr;     /* RCC clock enable register */
    volatile uint32_t *rstr;    /* RCC reset register (same bit) */
    uint32_t bit;
    const uint32_t *clock_hz;   /* kernel clock (0 = not available) */
} stm32_fdcan_t;

/* Family: the controller of `cfg`, its kernel clock selected and its TX / RX
 * pins set up; NULL if the part has no such instance. The message RAM layout
 * follows from the part (port/arch/stm32/fdcan.c). */
const stm32_fdcan_t *stm32_fdcan_get(const umcub_can_cfg_t *cfg);

#endif
