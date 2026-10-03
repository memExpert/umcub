/*
 * Portable Cortex-M reset entry for the bootloader. Replaces the weak
 * Reset_Handler of the CMSIS device startup file (whose vector table and
 * default handlers are kept).
 */
#include <stdint.h>
#include <string.h>
#include "umcub_cfg.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;
extern uint32_t g_pfnVectors[];
int main(void);

/* Dual-core SINGLE_BOOT: the second core parks here (never returns). */
void umcub_port_core2_park(void);

#define UMCUB_STR2(x) #x
#define UMCUB_STR(x) UMCUB_STR2(x)

__attribute__((noreturn, used)) static void c_start(void)
{
    uint32_t *src = &_sidata;
    for (uint32_t *dst = &_sdata; dst < &_edata;) {
        *dst++ = *src++;
    }
    for (uint32_t *dst = &_sbss; dst < &_ebss;) {
        *dst++ = 0;
    }
    *(volatile uint32_t *)0xE000ED08u = (uint32_t)g_pfnVectors;   /* SCB->VTOR */
    main();
    for (;;) {
    }
}

/* No stack is touched before MSP is set: on dual-core parts both cores
 * start here with the same vector[0]. */
__attribute__((naked, noreturn)) void Reset_Handler(void)
{
    __asm volatile(
#if UMCUB_CFG_DUALCORE_MODE == UMCUB_DUALCORE_SINGLE_BOOT && defined(UMCUB_FAMILY_CORE2_PARTNO)
        "ldr  r0, =0xE000ED00          \n"   /* SCB->CPUID */
        "ldr  r0, [r0]                 \n"
        "ubfx r0, r0, #4, #12          \n"   /* PARTNO */
        "ldr  r1, =" UMCUB_STR(UMCUB_FAMILY_CORE2_PARTNO) "\n"
        "cmp  r0, r1                   \n"
        "bne  1f                       \n"
        "ldr  r0, =_estack_core2       \n"
        "msr  msp, r0                  \n"
        "b    umcub_port_core2_park    \n"
        "1:                            \n"
#endif
        "ldr  r0, =_estack_main        \n"
        "msr  msp, r0                  \n"
        "b    c_start                  \n");
}
