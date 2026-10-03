/*
 * STM32F1 port: system services (SysTick, reset, jump, GPIO, watchdog).
 */
#include "f1.h"

static volatile uint32_t ticks;
static uint8_t reset_cause;
static uint32_t reset_flags;

/* RCC reset registers touched by drivers, restored on deinit. */
#define MAX_PERIPH 8
static struct {
    volatile uint32_t *rstr;
    uint32_t mask;
} used[MAX_PERIPH];
static unsigned used_n;

/* RCC enable registers as found at boot (restored on deinit). */
#define EN_REGS 3
static uint32_t en_snapshot[EN_REGS];
static volatile uint32_t *const en_regs[EN_REGS] = { &RCC->AHBENR, &RCC->APB1ENR, &RCC->APB2ENR };

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

void f1_periph_used(volatile uint32_t *rstr, uint32_t mask)
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

static uint8_t read_reset_cause(void)
{
    /* RCC_CSR flags are sticky until RMVF (RM0008 §7.3.10): latch and clear. */
    uint32_t csr = RCC->CSR;
    reset_flags = csr;
    RCC->CSR |= RCC_CSR_RMVF;
    if (csr & RCC_CSR_PORRSTF) {
        return UMCUB_RESET_POWER_ON;
    }
    if (csr & (RCC_CSR_IWDGRSTF | RCC_CSR_WWDGRSTF)) {
        return UMCUB_RESET_WATCHDOG;
    }
    if (csr & RCC_CSR_SFTRSTF) {
        return UMCUB_RESET_SOFTWARE;
    }
    if (csr & RCC_CSR_PINRSTF) {
        return UMCUB_RESET_PIN;
    }
    return UMCUB_RESET_OTHER;
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
#if UMCUB_CFG_USB
    /* Right after reset (before the HSE start-up), well within the host's
     * 100 ms attach debounce: the device must not look attached while its
     * USB is not running. */
    f1_usb_hold_detached();
#endif
    f1_clock_init();

    SysTick->LOAD = f1_sysclk_hz / 1000u - 1u;
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
    f1_clock_deinit();
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

/* --- watchdog: IWDG on LSI (~40 kHz, 30..60 kHz), RM0008 §19 --------------- */

void umcub_port_wdg_start(uint32_t timeout_ms)
{
    /* prescaler /256 -> ~156 Hz at 40 kHz -> 6.4 ms per count, 4095 max (26 s).
     * LSI spread makes the real timeout 0.67x..1.33x of this. */
    uint32_t reload = timeout_ms * 10u / 64u;
    if (reload == 0) {
        reload = 1;
    } else if (reload > 0xFFFu) {
        reload = 0xFFFu;
    }
    IWDG->KR = 0xCCCC;
    IWDG->KR = 0x5555;
    IWDG->PR = 6;
    IWDG->RLR = reload;
    (void)f1_wait(&IWDG->SR, IWDG_SR_PVU | IWDG_SR_RVU, 0, 100);
    IWDG->KR = 0xAAAA;
}

void umcub_port_wdg_feed(void)
{
    /* Always: a hardware watchdog (option byte WDG_SW = 0) runs regardless. */
    IWDG->KR = 0xAAAA;
}

/* --- GPIO (CRL/CRH, RM0008 §9.2) ------------------------------------------- */

GPIO_TypeDef *f1_gpio_port(uint32_t pin)
{
    unsigned port = UMCUB_PIN_PORT(pin);
    RCC->APB2ENR |= (RCC_APB2ENR_IOPAEN << port);
    (void)RCC->APB2ENR;
    return (GPIO_TypeDef *)(GPIOA_BASE + port * 0x400u);
}

void f1_gpio_config(uint32_t pin, uint32_t cnf_mode)
{
    GPIO_TypeDef *g = f1_gpio_port(pin);
    unsigned n = UMCUB_PIN_NUM(pin);
    volatile uint32_t *cr = n < 8 ? &g->CRL : &g->CRH;
    unsigned sh = (n & 7u) * 4u;
    MODIFY_REG(*cr, 0xFu << sh, cnf_mode << sh);
}

void umcub_port_gpio_input(uint32_t pin, int pull)
{
    GPIO_TypeDef *g = f1_gpio_port(pin);
    uint32_t m = 1u << UMCUB_PIN_NUM(pin);
    if (pull == UMCUB_PULL_NONE) {
        f1_gpio_config(pin, F1_GPIO_IN_FLOAT);
        return;
    }
    g->BSRR = pull == UMCUB_PULL_UP ? m : m << 16;
    f1_gpio_config(pin, F1_GPIO_IN_PULL);
}

bool umcub_port_gpio_read(uint32_t pin)
{
    GPIO_TypeDef *g = f1_gpio_port(pin);
    return (g->IDR >> UMCUB_PIN_NUM(pin)) & 1u;
}

/* F1 has no AF number: the peripheral owns the pin once it is AF output;
 * inputs of a peripheral are plain inputs. UMCUB_PIN(...) af is ignored. */
void umcub_port_gpio_af(uint32_t pin)
{
    f1_gpio_config(pin, F1_GPIO_AF_PP_50M);
}

void umcub_port_gpio_reset(uint32_t pin)
{
    GPIO_TypeDef *g = f1_gpio_port(pin);
    g->BRR = 1u << UMCUB_PIN_NUM(pin);
    f1_gpio_config(pin, F1_GPIO_IN_FLOAT);
}

void umcub_port_gpio_output(uint32_t pin, bool level)
{
    GPIO_TypeDef *g = f1_gpio_port(pin);
    uint32_t m = 1u << UMCUB_PIN_NUM(pin);
    g->BSRR = level ? m : m << 16;
    f1_gpio_config(pin, 0x2u);                      /* output push-pull, 2 MHz */
}

void umcub_port_gpio_write(uint32_t pin, bool level)
{
    GPIO_TypeDef *g = f1_gpio_port(pin);
    uint32_t m = 1u << UMCUB_PIN_NUM(pin);
    g->BSRR = level ? m : m << 16;
}

int umcub_port_rdp_level(void)
{
    return (FLASH->OBR & FLASH_OBR_RDPRT) ? 1 : 0;   /* F1: readout protection on/off */
}
