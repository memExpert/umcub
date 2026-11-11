/*
 * STM32G4 clock tree (RM0440 §7). Bootloader: SYSCLK 150 MHz from the main
 * PLL (input 4 MHz, VCO 300 MHz, /R 2), fed by the HSE or the 16 MHz HSI;
 * voltage range 1 normal mode (the reset setting: no boost mode needed up
 * to 150 MHz), APB1 = APB2 = 150 MHz. Deinit puts the reset configuration
 * back (HSI 16 MHz, PLL and HSE off, no wait states) so a CubeMX
 * SystemClock_Config() in the application starts from a known state.
 */
#include "g4.h"

uint32_t SystemCoreClock = 16000000u;
uint32_t g4_sysclk_hz = 16000000u;
uint32_t g4_pclk1_hz = 16000000u;
uint32_t g4_pclk2_hz = 16000000u;

#define HSI_HZ          16000000u
#define PLL_IN_HZ       4000000u        /* RM0440 §7.4.4: PLL input 2.66..16 MHz */
#define PLL_N           75u             /* VCO 300 MHz (96..344 MHz) */
#define PLL_SYS_HZ      150000000u      /* VCO / R, R = 2 */
#define PLLCFGR_RESET   0x00001000u     /* RM0440 §7.4.4 */
/* FLASH_ACR reset value 0x00040600 (RM0440 §3.7.1): caches on and DBG_SWEN
 * set. DBG_SWEN = 0 switches the debug port off until the next reset - the
 * register is therefore only ever modified field by field here. */
#define ACR_FIELDS      (FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN | FLASH_ACR_LATENCY)

#if UMCUB_CFG_CLOCK_SOURCE == UMCUB_CLK_HSE && \
    (UMCUB_CFG_HSE_HZ % PLL_IN_HZ || UMCUB_CFG_HSE_HZ < 4000000u || UMCUB_CFG_HSE_HZ > 48000000u)
#error "umcub: STM32G4 port supports a UMCUB_CFG_HSE_HZ of 4..48 MHz in steps of 4 MHz (DS12288: HSE 4..48 MHz)"
#endif

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

static uint32_t pllcfgr(uint32_t src_hz, uint32_t src_bits)
{
    return src_bits | ((src_hz / PLL_IN_HZ - 1u) << RCC_PLLCFGR_PLLM_Pos) | (PLL_N << RCC_PLLCFGR_PLLN_Pos) |
           RCC_PLLCFGR_PLLREN /* R = /2 (PLLR = 00) */;
}

void g4_clock_init(void)
{
    uint32_t cfg = pllcfgr(HSI_HZ, RCC_PLLCFGR_PLLSRC_HSI);
#if UMCUB_CFG_CLOCK_SOURCE == UMCUB_CLK_HSE
    if (UMCUB_CFG_HSE_BYPASS) {
        SET_BIT(RCC->CR, RCC_CR_HSEBYP);
    }
    SET_BIT(RCC->CR, RCC_CR_HSEON);
    if (wait_flag(&RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY)) {
        cfg = pllcfgr(UMCUB_CFG_HSE_HZ, RCC_PLLCFGR_PLLSRC_HSE);
    } else {
        CLEAR_BIT(RCC->CR, RCC_CR_HSEON);   /* no crystal: fall back to HSI */
        (void)wait_flag(&RCC->CR, RCC_CR_HSERDY, 0);
        CLEAR_BIT(RCC->CR, RCC_CR_HSEBYP);  /* writable only with the HSE stopped */
    }
#endif
    RCC->PLLCFGR = cfg;
    SET_BIT(RCC->CR, RCC_CR_PLLON);
    if (!wait_flag(&RCC->CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY)) {
        return;                             /* stay on HSI 16 MHz */
    }
    /* 4 wait states for 120..150 MHz in range 1 normal mode (RM0440
     * Table 9), set before the clock goes up; prefetch and caches on. */
    MODIFY_REG(FLASH->ACR, ACR_FIELDS, FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN | FLASH_ACR_LATENCY_4WS);
    (void)wait_flag(&FLASH->ACR, FLASH_ACR_LATENCY, FLASH_ACR_LATENCY_4WS);
    /* Going above 80 MHz: through AHB /2 for at least 1 us (RM0440 §7.2.7),
     * APB1 / APB2 /1. */
    MODIFY_REG(RCC->CFGR, RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2, RCC_CFGR_HPRE_DIV2);
    MODIFY_REG(RCC->CFGR, RCC_CFGR_SW, RCC_CFGR_SW_PLL);
    if (!wait_flag(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_PLL)) {
        MODIFY_REG(RCC->CFGR, RCC_CFGR_HPRE, 0);
        return;
    }
    for (volatile uint32_t n = 0; n < 200u; n++) {
        /* > 1 us at 75 MHz */
    }
    MODIFY_REG(RCC->CFGR, RCC_CFGR_HPRE, 0);
    g4_sysclk_hz = g4_pclk1_hz = g4_pclk2_hz = PLL_SYS_HZ;
    SystemCoreClock = g4_sysclk_hz;
}

void g4_clock_deinit(void)
{
    SET_BIT(RCC->CR, RCC_CR_HSION);
    (void)wait_flag(&RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY);
    MODIFY_REG(RCC->CFGR, RCC_CFGR_SW, RCC_CFGR_SW_HSI);
    (void)wait_flag(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_HSI);
    RCC->CFGR = RCC_CFGR_SW_HSI;            /* reset value: prescalers /1, MCO off */
    CLEAR_BIT(RCC->CR, RCC_CR_PLLON);
    (void)wait_flag(&RCC->CR, RCC_CR_PLLRDY, 0);
    RCC->PLLCFGR = PLLCFGR_RESET;
    CLEAR_BIT(RCC->CR, RCC_CR_HSEON | RCC_CR_CSSON);
    (void)wait_flag(&RCC->CR, RCC_CR_HSERDY, 0);   /* HSEBYP is writable only with the HSE stopped */
    CLEAR_BIT(RCC->CR, RCC_CR_HSEBYP);
    RCC->CIER = 0;                          /* ready / CSS interrupts off, flags cleared */
    RCC->CICR = 0x0000073Fu;
    /* Zero wait states, no prefetch; caches flushed (they must be off for
     * that) and back on - the reset state, DBG_SWEN untouched. */
    MODIFY_REG(FLASH->ACR, ACR_FIELDS, 0);
    SET_BIT(FLASH->ACR, FLASH_ACR_ICRST | FLASH_ACR_DCRST);
    CLEAR_BIT(FLASH->ACR, FLASH_ACR_ICRST | FLASH_ACR_DCRST);
    SET_BIT(FLASH->ACR, FLASH_ACR_ICEN | FLASH_ACR_DCEN);
    SystemCoreClock = g4_sysclk_hz = g4_pclk1_hz = g4_pclk2_hz = HSI_HZ;
}
