/*
 * STM32F1 clock tree (RM0008 §7). Bootloader: SYSCLK from the PLL, 72 MHz
 * from an 8/12/16 MHz HSE, or 64 MHz from HSI/2; deinit puts the reset
 * configuration back (HSI 8 MHz, PLL and HSE off, no wait states) so a
 * CubeMX SystemClock_Config() in the application starts from a known state.
 */
#include "f1.h"

uint32_t SystemCoreClock = 8000000u;
uint32_t f1_sysclk_hz = 8000000u;
uint32_t f1_pclk1_hz = 8000000u;
uint32_t f1_pclk2_hz = 8000000u;

/* Busy loops before SysTick runs: bounded by an iteration count. */
static bool wait_flag(volatile uint32_t *reg, uint32_t mask, uint32_t value)
{
    for (uint32_t n = 0; n < 2000000u; n++) {
        if ((*reg & mask) == value) {
            return true;
        }
    }
    return false;
}

static void use_hsi_pll(void)
{
    /* HSI/2 x16 = 64 MHz (the maximum from HSI). */
    MODIFY_REG(RCC->CFGR, RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL, RCC_CFGR_PLLMULL16);
    f1_sysclk_hz = 64000000u;
}

void f1_clock_init(void)
{
    bool hse = false;
#if UMCUB_CFG_CLOCK_SOURCE == UMCUB_CLK_HSE
    /* PLL input 4..16 MHz (HSE or HSE/2), output 72 MHz. Connectivity-line
     * parts (F105/F107, PREDIV1) are not covered. */
#if UMCUB_CFG_HSE_HZ == 8000000
#define F1_PLL_CFG RCC_CFGR_PLLMULL9
#elif UMCUB_CFG_HSE_HZ == 12000000
#define F1_PLL_CFG RCC_CFGR_PLLMULL6
#elif UMCUB_CFG_HSE_HZ == 16000000
#define F1_PLL_CFG (RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL9)
#else
#error "umcub: STM32F1 port supports UMCUB_CFG_HSE_HZ of 8, 12 or 16 MHz"
#endif
    if (UMCUB_CFG_HSE_BYPASS) {
        SET_BIT(RCC->CR, RCC_CR_HSEBYP);
    }
    SET_BIT(RCC->CR, RCC_CR_HSEON);
    hse = wait_flag(&RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY);
    if (hse) {
        MODIFY_REG(RCC->CFGR, RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL,
                   RCC_CFGR_PLLSRC | F1_PLL_CFG);
        f1_sysclk_hz = 72000000u;
    } else {
        CLEAR_BIT(RCC->CR, RCC_CR_HSEON);   /* no crystal: fall back to HSI */
        CLEAR_BIT(RCC->CR, RCC_CR_HSEBYP);  /* writable only with HSE off */
    }
#endif
    if (!hse) {
        use_hsi_pll();
    }

    /* 2 wait states above 48 MHz (RM0008 §3.3.3), prefetch on. */
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_1;
    /* AHB /1, APB2 /1 (<= 72 MHz), APB1 /2 (<= 36 MHz). */
    MODIFY_REG(RCC->CFGR, RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2, RCC_CFGR_PPRE1_DIV2);

    SET_BIT(RCC->CR, RCC_CR_PLLON);
    if (!wait_flag(&RCC->CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY)) {
        f1_sysclk_hz = 8000000u;            /* stay on HSI */
    } else {
        MODIFY_REG(RCC->CFGR, RCC_CFGR_SW, RCC_CFGR_SW_PLL);
        (void)wait_flag(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_PLL);
    }
    f1_pclk2_hz = f1_sysclk_hz;
    f1_pclk1_hz = f1_sysclk_hz > 36000000u ? f1_sysclk_hz / 2u : f1_sysclk_hz;
    if (f1_sysclk_hz <= 36000000u) {
        MODIFY_REG(RCC->CFGR, RCC_CFGR_PPRE1, 0);
    }
    SystemCoreClock = f1_sysclk_hz;
}

void f1_clock_deinit(void)
{
    SET_BIT(RCC->CR, RCC_CR_HSION);
    (void)wait_flag(&RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY);
    MODIFY_REG(RCC->CFGR, RCC_CFGR_SW, RCC_CFGR_SW_HSI);
    (void)wait_flag(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_HSI);
    RCC->CFGR = 0;                          /* prescalers /1, PLL config reset, MCO off */
    CLEAR_BIT(RCC->CR, RCC_CR_PLLON);
    (void)wait_flag(&RCC->CR, RCC_CR_PLLRDY, 0);
    CLEAR_BIT(RCC->CR, RCC_CR_HSEON | RCC_CR_CSSON);
    CLEAR_BIT(RCC->CR, RCC_CR_HSEBYP);
    RCC->CIR = 0x009F0000u;                 /* clear ready/CSS flags, interrupts off */
    FLASH->ACR = FLASH_ACR_PRFTBE;          /* zero wait states (reset value 0x30) */
    SystemCoreClock = f1_sysclk_hz = f1_pclk1_hz = f1_pclk2_hz = 8000000u;
}
