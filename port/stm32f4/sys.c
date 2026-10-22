/*
 * STM32F4 port: system services (reset cause, clocks, GPIO, watchdog); the
 * Cortex-M part (SysTick, deinit, jump) is port/arch/cortexm/cortexm_sys.c.
 */
#include "f4.h"
#include "stm32f4xx_ll_gpio.h"

static uint8_t reset_cause;
static uint32_t reset_flags;

/* RCC enable registers as found at boot (restored on deinit). */
static volatile uint32_t *const en_regs[] = {
    &RCC->AHB1ENR, &RCC->AHB2ENR, &RCC->APB1ENR, &RCC->APB2ENR,
};

static uint8_t read_reset_cause(void)
{
    /* RCC_CSR flags are sticky until RMVF (RM0383 §6.3.18): latch and clear. */
    uint32_t csr = RCC->CSR;
    reset_flags = csr;
    RCC->CSR |= RCC_CSR_RMVF;
    if (csr & RCC_CSR_BORRSTF) {
        return (csr & RCC_CSR_PORRSTF) ? UMCUB_RESET_POWER_ON : UMCUB_RESET_BROWNOUT;
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
    umcub_cm_save_clocks(en_regs, sizeof(en_regs) / sizeof(en_regs[0]));
    f4_clock_init();
    umcub_cm_start(f4_sysclk_hz);
}

void umcub_port_deinit(void)
{
    umcub_cm_deinit();
    f4_clock_deinit();
    __DSB();
    __ISB();
}

/* --- watchdog: IWDG on LSI (32 kHz, 17..47 kHz, DS10314 Table 40) ---------- */

void umcub_port_wdg_start(uint32_t timeout_ms)
{
    stm32_iwdg_start(IWDG, 32000u, timeout_ms);
}

void umcub_port_wdg_feed(void)
{
    stm32_iwdg_feed(IWDG);
}

/* --- GPIO (RM0383 §8) ------------------------------------------------------ */

static GPIO_TypeDef *gpio_port(uint32_t pin)
{
    unsigned port = UMCUB_PIN_PORT(pin);
    RCC->AHB1ENR |= (RCC_AHB1ENR_GPIOAEN << port);
    (void)RCC->AHB1ENR;                     /* ES0287 2.2.7: delay after the clock enable */
    return (GPIO_TypeDef *)(GPIOA_BASE + port * 0x400u);
}

void umcub_port_gpio_input(uint32_t pin, int pull)
{
    GPIO_TypeDef *g = gpio_port(pin);
    uint32_t m = 1u << UMCUB_PIN_NUM(pin);
    LL_GPIO_SetPinMode(g, m, LL_GPIO_MODE_INPUT);
    LL_GPIO_SetPinPull(g, m, pull == UMCUB_PULL_UP ? LL_GPIO_PULL_UP :
                             pull == UMCUB_PULL_DOWN ? LL_GPIO_PULL_DOWN : LL_GPIO_PULL_NO);
}

void f4_gpio_pull_up(uint32_t pin)
{
    LL_GPIO_SetPinPull(gpio_port(pin), 1u << UMCUB_PIN_NUM(pin), LL_GPIO_PULL_UP);
}

bool umcub_port_gpio_read(uint32_t pin)
{
    GPIO_TypeDef *g = gpio_port(pin);
    return (g->IDR >> UMCUB_PIN_NUM(pin)) & 1u;
}

void umcub_port_gpio_af(uint32_t pin)
{
    GPIO_TypeDef *g = gpio_port(pin);
    uint32_t m = 1u << UMCUB_PIN_NUM(pin);
    LL_GPIO_SetPinSpeed(g, m, LL_GPIO_SPEED_FREQ_HIGH);
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
    GPIO_TypeDef *g = gpio_port(pin);
    uint32_t m = 1u << UMCUB_PIN_NUM(pin);
    LL_GPIO_SetPinMode(g, m, LL_GPIO_MODE_INPUT);  /* reset state on the F4 (RM0383 §8.4.1), not analog */
    LL_GPIO_SetPinPull(g, m, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinSpeed(g, m, LL_GPIO_SPEED_FREQ_LOW);
    if (UMCUB_PIN_NUM(pin) < 8) {
        LL_GPIO_SetAFPin_0_7(g, m, 0);
    } else {
        LL_GPIO_SetAFPin_8_15(g, m, 0);
    }
}

void umcub_port_gpio_output(uint32_t pin, bool level)
{
    GPIO_TypeDef *g = gpio_port(pin);
    uint32_t m = 1u << UMCUB_PIN_NUM(pin);
    g->BSRR = level ? m : m << 16;
    LL_GPIO_SetPinOutputType(g, m, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(g, m, LL_GPIO_SPEED_FREQ_LOW);
    LL_GPIO_SetPinPull(g, m, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinMode(g, m, LL_GPIO_MODE_OUTPUT);
}

void umcub_port_gpio_write(uint32_t pin, bool level)
{
    GPIO_TypeDef *g = gpio_port(pin);
    uint32_t m = 1u << UMCUB_PIN_NUM(pin);
    g->BSRR = level ? m : m << 16;
}

int umcub_port_rdp_level(void)
{
    /* FLASH_OPTCR.RDP: 0xAA level 0, 0xCC level 2, anything else level 1 (RM0383 §3.6). */
    uint32_t rdp = (FLASH->OPTCR & FLASH_OPTCR_RDP) >> FLASH_OPTCR_RDP_Pos;
    return rdp == 0xAAu ? 0 : rdp == 0xCCu ? 2 : 1;
}
