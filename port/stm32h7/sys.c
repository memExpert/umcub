/*
 * STM32H7 port: system services (SysTick, reset, jump, GPIO, watchdog).
 */
#include "h7.h"

static volatile uint32_t ticks;
static uint8_t reset_cause;
static bool keep_clocks;

/* RCC reset registers touched by drivers, restored on deinit. */
#define MAX_PERIPH 12
static struct {
    volatile uint32_t *rstr;
    uint32_t mask;
} used[MAX_PERIPH];
static unsigned used_n;

/* RCC enable registers as found at boot (restored on deinit). */
#define EN_REGS 7
static uint32_t en_snapshot[EN_REGS];
static volatile uint32_t *const en_regs[EN_REGS] = {
    &RCC->AHB1ENR, &RCC->AHB2ENR, &RCC->AHB4ENR, &RCC->APB1LENR, &RCC->APB1HENR, &RCC->APB2ENR,
    &RCC->APB4ENR,
};

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

void h7_periph_used(volatile uint32_t *rstr, uint32_t mask)
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

static uint32_t reset_flags;

static uint8_t read_reset_cause(void)
{
    /* Flags are sticky until RMVF: latch and clear them, otherwise a POR
     * flag would be seen on every later reset. The raw value is passed to
     * the application in the handoff (umcub_handoff_t.reset_flags). */
    uint32_t rsr = RCC->RSR;
    reset_flags = rsr;
    RCC->RSR |= RCC_RSR_RMVF;
    if (rsr & RCC_RSR_BORRSTF) {
        return (rsr & RCC_RSR_PORRSTF) ? UMCUB_RESET_POWER_ON : UMCUB_RESET_BROWNOUT;
    }
#if H7_IS_CM4
    if (rsr & (RCC_RSR_IWDG2RSTF | RCC_RSR_WWDG2RSTF)) {
        return UMCUB_RESET_WATCHDOG;
    }
    if (rsr & RCC_RSR_SFT2RSTF) {
        return UMCUB_RESET_SOFTWARE;
    }
#else
#if defined(RCC_RSR_IWDG1RSTF)
    if (rsr & (RCC_RSR_IWDG1RSTF | RCC_RSR_WWDG1RSTF)) {
        return UMCUB_RESET_WATCHDOG;
    }
#endif
#if defined(RCC_RSR_SFT1RSTF)
    if (rsr & RCC_RSR_SFT1RSTF) {
        return UMCUB_RESET_SOFTWARE;
    }
#elif defined(RCC_RSR_SFTRSTF)
    if (rsr & RCC_RSR_SFTRSTF) {
        return UMCUB_RESET_SOFTWARE;
    }
#endif
#endif
    if (rsr & RCC_RSR_PINRSTF) {
        return UMCUB_RESET_PIN;
    }
    return UMCUB_RESET_OTHER;
}

void umcub_port_keep_clocks(bool keep)
{
    keep_clocks = keep;
}

uint32_t umcub_port_reset_flags(void)
{
    return reset_flags;
}

uint8_t umcub_port_reset_cause(void)
{
    return reset_cause;
}

void umcub_port_init(void)
{
    reset_cause = read_reset_cause();
    for (unsigned i = 0; i < EN_REGS; i++) {
        en_snapshot[i] = *en_regs[i];
    }

#if !H7_IS_CM4
    h7_clock_init();
    /* I-cache on, D-cache stays off: DMA buffers need no maintenance. */
    SCB_EnableICache();
#else
    /* CM4 (PER_CORE): clock tree is owned by the CM7 instance. */
    h7_sysclk_hz = h7_hclk_hz = 200000000;
    h7_pclk_hz = 100000000;
    h7_pll1q_hz = 100000000;
    SystemCoreClock = h7_sysclk_hz;
#endif

    SysTick->LOAD = h7_sysclk_hz / 1000u - 1u;
    SysTick->VAL = 0;
    NVIC_SetPriority(SysTick_IRQn, 0);
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
    __enable_irq();
}

void umcub_port_deinit(void)
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
    for (unsigned i = 0; i < EN_REGS; i++) {
        *en_regs[i] = en_snapshot[i];
    }

#if !H7_IS_CM4
    if (!keep_clocks) {
        h7_clock_deinit();
    }
    SCB_DisableICache();
#endif
    __DSB();
    __ISB();
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

/* --- watchdog (IWDG1 for CM7, IWDG2 for CM4), LSI = 32 kHz ---------------- */

#if H7_IS_CM4
#define IWDG_INST IWDG2
#else
#define IWDG_INST IWDG1
#endif

void umcub_port_wdg_start(uint32_t timeout_ms)
{
    /* prescaler /256 -> 125 Hz -> 8 ms per count, max 4095 counts (32.7 s) */
    uint32_t reload = timeout_ms / 8u;
    if (reload == 0) {
        reload = 1;
    } else if (reload > 0xFFFu) {
        reload = 0xFFFu;
    }
    IWDG_INST->KR = 0xCCCC;
    IWDG_INST->KR = 0x5555;
    IWDG_INST->PR = 6;
    IWDG_INST->RLR = reload;
    (void)h7_wait(&IWDG_INST->SR, 0x7u, 0, 100);
    IWDG_INST->KR = 0xAAAA;
}

void umcub_port_wdg_feed(void)
{
    /* Always: the IWDG may run without UMCUB_CFG_WATCHDOG_MS (hardware
     * watchdog option byte IWDGx_SW = 0). Reloading a stopped IWDG is a no-op. */
    IWDG_INST->KR = 0xAAAA;
}

/* --- GPIO ----------------------------------------------------------------- */

GPIO_TypeDef *h7_gpio_port(uint32_t pin)
{
    unsigned port = UMCUB_PIN_PORT(pin);
    RCC->AHB4ENR |= (RCC_AHB4ENR_GPIOAEN << port);
    (void)RCC->AHB4ENR;
    return (GPIO_TypeDef *)(GPIOA_BASE + port * 0x400u);
}

void umcub_port_gpio_input(uint32_t pin, int pull)
{
    GPIO_TypeDef *g = h7_gpio_port(pin);
    uint32_t n = UMCUB_PIN_NUM(pin);
    LL_GPIO_SetPinMode(g, 1u << n, LL_GPIO_MODE_INPUT);
    LL_GPIO_SetPinPull(g, 1u << n, pull == UMCUB_PULL_UP ? LL_GPIO_PULL_UP :
                                   pull == UMCUB_PULL_DOWN ? LL_GPIO_PULL_DOWN : LL_GPIO_PULL_NO);
}

bool umcub_port_gpio_read(uint32_t pin)
{
    GPIO_TypeDef *g = h7_gpio_port(pin);
    return (g->IDR >> UMCUB_PIN_NUM(pin)) & 1u;
}

void umcub_port_gpio_af(uint32_t pin)
{
    GPIO_TypeDef *g = h7_gpio_port(pin);
    uint32_t m = 1u << UMCUB_PIN_NUM(pin);
    LL_GPIO_SetPinSpeed(g, m, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinOutputType(g, m, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinPull(g, m, LL_GPIO_PULL_NO);
    if (UMCUB_PIN_NUM(pin) < 8) {
        LL_GPIO_SetAFPin_0_7(g, m, UMCUB_PIN_AF(pin));
    } else {
        LL_GPIO_SetAFPin_8_15(g, m, UMCUB_PIN_AF(pin));
    }
    LL_GPIO_SetPinMode(g, m, LL_GPIO_MODE_ALTERNATE);
}

void umcub_port_gpio_reset(uint32_t pin)
{
    GPIO_TypeDef *g = h7_gpio_port(pin);
    uint32_t m = 1u << UMCUB_PIN_NUM(pin);
    LL_GPIO_SetPinMode(g, m, LL_GPIO_MODE_ANALOG);
    LL_GPIO_SetPinPull(g, m, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinSpeed(g, m, LL_GPIO_SPEED_FREQ_LOW);
    if (UMCUB_PIN_NUM(pin) < 8) {
        LL_GPIO_SetAFPin_0_7(g, m, 0);
    } else {
        LL_GPIO_SetAFPin_8_15(g, m, 0);
    }
}
