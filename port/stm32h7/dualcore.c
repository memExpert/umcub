/*
 * STM32H745/H755/H747/H757 dual-core support (RM0399 §11 HSEM).
 *
 * UMCUB_DUALCORE_SINGLE_BOOT: both cores boot from the bootloader vector
 * table (BOOT_CM4_ADD0 = BOOT_CM7_ADD0). Reset_Handler (boot/startup.c)
 * checks SCB->CPUID before any stack access and sends the CM4 to
 * umcub_port_core2_park() on its own stack: the CM4 parks in WFE until the CM7 publishes the CM4 image's
 * vector table in the handoff area and releases HSEM_SEM_CM4_GO.
 *
 * The bootloader is compiled for the CM7 with soft-float, so this code is
 * plain ARMv7-M Thumb-2 and runs unchanged on the CM4.
 */
#include "h7.h"

#if defined(UMCUB_FAMILY_DUALCORE)

#include "umcub_handoff.h"

#define HSEM_SEM_CM4_GO       28u
#define HSEM_SEM_EVT_BASE     29u    /* 29: clocks ready, 30: CM4 boot done */
#define HSEM_C1               ((HSEM_Common_TypeDef *)(HSEM_BASE + 0x100UL))
#define HSEM_C2               ((HSEM_Common_TypeDef *)(HSEM_BASE + 0x110UL))
#define CPUID_PART_CM4        0xC24u
#define CORE_ID_CM7           3u     /* HSEM COREID values (RM0399 §11.4.4) */
#define CORE_ID_CM4           1u

static bool running_on_cm4(void)
{
    return ((SCB->CPUID >> SCB_CPUID_PARTNO_Pos) & 0xFFFu) == CPUID_PART_CM4;
}

/* Take and immediately release a semaphore: the release raises the
 * "semaphore free" interrupt on every core that enabled it. */
static void hsem_pulse(uint32_t sem, uint32_t core_id)
{
    RCC->AHB4ENR |= RCC_AHB4ENR_HSEMEN;
    (void)RCC->AHB4ENR;
    (void)HSEM->RLR[sem];                          /* 1-step lock */
    HSEM->R[sem] = core_id << HSEM_R_COREID_Pos;   /* release, PROCID 0 */
}

#if UMCUB_CFG_DUALCORE_MODE == UMCUB_DUALCORE_SINGLE_BOOT

__attribute__((noreturn, used)) void umcub_port_core2_park(void)
{
    volatile umcub_handoff_t *h = (volatile umcub_handoff_t *)UMCUB_CFG_SHARED_RAM_ADDR;
    uint32_t bit = 1u << HSEM_SEM_CM4_GO;

    RCC_C2->AHB4ENR |= RCC_AHB4ENR_HSEMEN;
    (void)RCC_C2->AHB4ENR;
    HSEM_C2->ICR = bit;
    HSEM_C2->IER |= bit;
    SCB->SCR |= SCB_SCR_SEVONPEND_Msk;   /* pending IRQ wakes WFE, no ISR */

    for (;;) {
        if (HSEM_C2->ISR & bit) {
            HSEM_C2->ICR = bit;
            NVIC_ClearPendingIRQ(HSEM2_IRQn);
            uint32_t vtor = h->core2_vtor;
            if (h->core2_magic == UMCUB_CORE2_MAGIC && (vtor & 0x3FFu) == 0) {
                h->core2_magic = 0;
                HSEM_C2->IER &= ~bit;
                SCB->SCR &= ~SCB_SCR_SEVONPEND_Msk;
                RCC_C2->AHB4ENR &= ~RCC_AHB4ENR_HSEMEN;
                umcub_port_jump(vtor);
            }
        }
        __WFE();
    }
}

void umcub_port_core2_release(uint32_t vtor)
{
    volatile umcub_handoff_t *h = (volatile umcub_handoff_t *)UMCUB_CFG_SHARED_RAM_ADDR;
    h->core2_vtor = vtor;
    h->core2_magic = UMCUB_CORE2_MAGIC;
    __DSB();
    hsem_pulse(HSEM_SEM_CM4_GO, CORE_ID_CM7);
}

#else /* PER_CORE */

void umcub_port_core2_release(uint32_t vtor)
{
    (void)vtor;
}

#endif

void umcub_port_core_signal(unsigned event)
{
    hsem_pulse(HSEM_SEM_EVT_BASE + event, running_on_cm4() ? CORE_ID_CM4 : CORE_ID_CM7);
}

bool umcub_port_core_wait(unsigned event, uint32_t timeout_ms)
{
    HSEM_Common_TypeDef *c = running_on_cm4() ? HSEM_C2 : HSEM_C1;
    uint32_t bit = 1u << (HSEM_SEM_EVT_BASE + event);
    if (running_on_cm4()) {
        RCC_C2->AHB4ENR |= RCC_AHB4ENR_HSEMEN;
    } else {
        RCC_C1->AHB4ENR |= RCC_AHB4ENR_HSEMEN;
    }
    c->IER |= bit;
    uint32_t start = umcub_port_millis();
    bool ok = false;
    for (;;) {
        if (c->ISR & bit) {
            c->ICR = bit;
            ok = true;
            break;
        }
        /* Before SysTick runs (CM4 waiting for clocks) millis() is 0: wait forever. */
        if (timeout_ms && (uint32_t)(umcub_port_millis() - start) > timeout_ms) {
            break;
        }
    }
    c->IER &= ~bit;
    return ok;
}

#endif /* UMCUB_FAMILY_DUALCORE */
