/*
 * Cortex-M services shared by the family ports (CMSIS-Core only): SysTick
 * time base, reset / jump, peripheral bookkeeping for deinit, UID, waits.
 * Included by the family's private header after its device header.
 */
#ifndef UMCUB_CORTEXM_PORT_H
#define UMCUB_CORTEXM_PORT_H

#include <stdint.h>

/* Busy-wait until (*reg & mask) == value or timeout (UMCUB_ETIMEOUT). */
int umcub_cm_wait(volatile uint32_t *reg, uint32_t mask, uint32_t value, uint32_t timeout_ms);

/* Peripheral reset bookkeeping: drivers register the RCC reset bit of every
 * peripheral they enable so deinit can put it back into reset state. */
void umcub_cm_periph_used(volatile uint32_t *rstr, uint32_t mask);

/* First thing at init: the RCC enable registers to restore at deinit (at
 * most UMCUB_CM_EN_REGS_MAX), saved as found at reset. */
#define UMCUB_CM_EN_REGS_MAX 8
void umcub_cm_save_clocks(volatile uint32_t *const *en_regs, unsigned n);

/* After the clock setup: SysTick at 1 kHz from `cpu_hz`, interrupts on. */
void umcub_cm_start(uint32_t cpu_hz);

/* Interrupts off, SysTick off, NVIC cleared, registered peripherals reset,
 * RCC enable registers back to their values at init. */
void umcub_cm_deinit(void);

#endif /* UMCUB_CORTEXM_PORT_H */
