/*
 * Cortex-M system services of the bootloader shared by the family ports:
 * SysTick time base, deinit before the jump, reset, jump, UID.
 */
#include "umcub_family_cmsis.h"
#include "umcub_port.h"
#include "cortexm_port.h"

static volatile uint32_t ticks;

/* RCC reset registers touched by drivers, restored on deinit. */
#define MAX_PERIPH 12
static struct {
    volatile uint32_t *rstr;
    uint32_t mask;
} used[MAX_PERIPH];
static unsigned used_n;

/* RCC enable registers as found at boot (restored on deinit). */
static volatile uint32_t *const *en_regs;
static unsigned en_n;
static uint32_t en_snapshot[UMCUB_CM_EN_REGS_MAX];

void SysTick_Handler(void)
{
    ticks++;
}

uint32_t umcub_port_millis(void)
{
    return ticks;
}

void umcub_port_delay_ms(uint32_t ms)
{
    uint32_t start = ticks;
    while ((uint32_t)(ticks - start) < ms) {
        umcub_port_idle();
    }
}

void umcub_cm_periph_used(volatile uint32_t *rstr, uint32_t mask)
{
    for (unsigned i = 0; i < used_n; i++) {
        if (used[i].rstr == rstr) {
            used[i].mask |= mask;
            return;
        }
    }
    if (used_n < MAX_PERIPH) {
        used[used_n].rstr = rstr;
        used[used_n].mask = mask;
        used_n++;
    }
}

void umcub_cm_save_clocks(volatile uint32_t *const *regs, unsigned n)
{
    en_regs = regs;
    en_n = n < UMCUB_CM_EN_REGS_MAX ? n : UMCUB_CM_EN_REGS_MAX;
    for (unsigned i = 0; i < en_n; i++) {
        en_snapshot[i] = *en_regs[i];
    }
}

void umcub_cm_start(uint32_t cpu_hz)
{
    SysTick->LOAD = cpu_hz / 1000u - 1u;
    SysTick->VAL = 0;
    NVIC_SetPriority(SysTick_IRQn, 0);
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
    __enable_irq();
}

void umcub_cm_deinit(void)
{
    __disable_irq();

    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;

    for (unsigned i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;

    for (unsigned i = 0; i < used_n; i++) {
        SET_BIT(*used[i].rstr, used[i].mask);
        (void)*used[i].rstr;
        CLEAR_BIT(*used[i].rstr, used[i].mask);
    }
    used_n = 0;
    for (unsigned i = 0; i < en_n; i++) {
        *en_regs[i] = en_snapshot[i];
    }
}

__attribute__((noreturn)) void umcub_port_reset(void)
{
    NVIC_SystemReset();
}

__attribute__((noreturn)) void umcub_port_jump(uint32_t vtor)
{
    const uint32_t *vt = (const uint32_t *)vtor;
    uint32_t sp = vt[0];
    uint32_t pc = vt[1];

    SCB->VTOR = vtor;
    __DSB();
    __ISB();
    __asm volatile(
        "msr msp, %0      \n"
        "movs r1, #0      \n"
        "msr control, r1  \n"
        "isb              \n"
        "cpsie i          \n"
        "bx %1            \n"
        :
        : "r"(sp), "r"(pc)
        : "r1", "memory");
    __builtin_unreachable();
}

void umcub_port_uid(uint8_t uid[12])
{
    const uint8_t *p = (const uint8_t *)UID_BASE;
    for (unsigned i = 0; i < 12; i++) {
        uid[i] = p[i];
    }
}

__attribute__((weak)) void umcub_port_idle(void)
{
}
