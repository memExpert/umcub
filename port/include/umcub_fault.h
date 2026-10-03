/*
 * TEST ONLY: power-loss emulation for the bootloader (UMCUB_CFG_TEST_FAULT_INJECT).
 *
 * The host (tools/hw/powerfail_test.py) writes { MAGIC, target K, 0, 0 } to
 * UMCUB_CFG_TEST_FAULT_ADDR through SWD. Every fault point in the flash
 * driver increments `count`; at count == K the MCU resets immediately - in
 * the middle of a running sector erase / flash-word program, like a power
 * cut. `fired` stays set so the next boots run normally. target = 0 only
 * counts the operations (to size the test).
 * All stores are 32-bit (ECC SRAM: narrower stores are lost on reset).
 */
#ifndef UMCUB_FAULT_H
#define UMCUB_FAULT_H

#include <stdint.h>
#include "umcub_cfg.h"

#if UMCUB_CFG_TEST_FAULT_INJECT && !defined(UMCUB_BUILDING_APP)

#define UMCUB_FAULT_MAGIC 0xFA017E57u

typedef struct {
    uint32_t magic;
    uint32_t target;
    uint32_t count;
    uint32_t fired;
} umcub_fault_t;

static inline void umcub_fault_point(void)
{
    volatile umcub_fault_t *f = (volatile umcub_fault_t *)UMCUB_CFG_TEST_FAULT_ADDR;
    if (f->magic != UMCUB_FAULT_MAGIC || f->fired) {
        return;
    }
    uint32_t n = f->count + 1u;
    f->count = n;
    if (n == f->target) {
        f->fired = 1u;
        __asm volatile("dsb" ::: "memory");
        /* SCB->AIRCR = VECTKEY | SYSRESETREQ, no waiting for anything. */
        *(volatile uint32_t *)0xE000ED0Cu = (0x5FAu << 16) | (*(volatile uint32_t *)0xE000ED0Cu & (7u << 8)) | (1u << 2);
        __asm volatile("dsb" ::: "memory");
        for (;;) {
        }
    }
}
#define UMCUB_FAULT_POINT() umcub_fault_point()
#else
#define UMCUB_FAULT_POINT() do { } while (0)
#endif

#endif /* UMCUB_FAULT_H */
