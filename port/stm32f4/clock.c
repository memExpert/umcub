/*
 * STM32F4 clock tree (RM0383 §6). Bootloader: SYSCLK 96 MHz from the main
 * PLL (VCO 192 MHz: /2 for the system, /4 = 48 MHz for USB OTG FS), fed by
 * the HSE or the 16 MHz HSI; deinit puts the reset configuration back (HSI
 * 16 MHz, PLL and HSE off, no wait states, regulator scale 2) so a CubeMX
 * SystemClock_Config() in the application starts from a known state.
 */
#include "f4.h"

uint32_t SystemCoreClock = 16000000u;
uint32_t f4_sysclk_hz = 16000000u;
uint32_t f4_pclk1_hz = 16000000u;
uint32_t f4_pclk2_hz = 16000000u;

#define HSI_HZ          16000000u
#define PLLCFGR_RESET   0x24003010u     /* RM0383 §6.3.2 */

/* PLL input divided down to 2 MHz when the source allows it (recommended,
 * least jitter), else to 1 MHz; RM0383 §6.3.2: VCO input 1..2 MHz, VCO
 * output 100..432 MHz, PLLM 2..63, PLLN 50..432. */
#define PLL_IN_HZ(src)  ((src) % 2000000u == 0 ? 2000000u : 1000000u)
#define PLL_VCO_HZ      192000000u

#if UMCUB_CFG_CLOCK_SOURCE == UMCUB_CLK_HSE && \
    (UMCUB_CFG_HSE_HZ % 1000000u || UMCUB_CFG_HSE_HZ < 4000000u || UMCUB_CFG_HSE_HZ > 26000000u)
#error "umcub: STM32F4 port supports a whole-MHz UMCUB_CFG_HSE_HZ of 4..26 MHz (DS10314: HSE 4..26 MHz)"
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

static uint32_t pllcfgr(uint32_t src_hz, uint32_t src_bit)
{
    uint32_t in = PLL_IN_HZ(src_hz);
    return src_bit | (src_hz / in) << RCC_PLLCFGR_PLLM_Pos | (PLL_VCO_HZ / in) << RCC_PLLCFGR_PLLN_Pos |
           0u << RCC_PLLCFGR_PLLP_Pos /* /2 */ | 4u << RCC_PLLCFGR_PLLQ_Pos /* /4 */;
}

void f4_clock_init(void)
{
    uint32_t cfg = pllcfgr(HSI_HZ, 0);
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
    /* Regulator scale 1 for more than 84 MHz (RM0383 §5.4.1); VOS is
     * writable only with the PLL off and takes effect when it runs. */
    SET_BIT(RCC->APB1ENR, RCC_APB1ENR_PWREN);
    (void)RCC->APB1ENR;
    MODIFY_REG(PWR->CR, PWR_CR_VOS, PWR_CR_VOS);
    /* the fields only: reserved bits keep their reset value (RM0383 §6.3.2) */
    MODIFY_REG(RCC->PLLCFGR, RCC_PLLCFGR_PLLM | RCC_PLLCFGR_PLLN | RCC_PLLCFGR_PLLP | RCC_PLLCFGR_PLLSRC |
               RCC_PLLCFGR_PLLQ, cfg);
    SET_BIT(RCC->CR, RCC_CR_PLLON);
    if (!wait_flag(&RCC->CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY) ||
        !wait_flag(&PWR->CSR, PWR_CSR_VOSRDY, PWR_CSR_VOSRDY)) {
        return;                             /* stay on HSI 16 MHz */
    }
    /* 3 wait states for 90..100 MHz at 2.7..3.6 V (RM0383 Table 5), set
     * before the clock goes up; prefetch and caches on. */
    FLASH->ACR = FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN | FLASH_ACR_LATENCY_3WS;
    (void)wait_flag(&FLASH->ACR, FLASH_ACR_LATENCY, FLASH_ACR_LATENCY_3WS);
    /* AHB /1, APB2 /1 (<= 100 MHz), APB1 /2 (<= 50 MHz). */
    MODIFY_REG(RCC->CFGR, RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2, RCC_CFGR_PPRE1_DIV2);
    MODIFY_REG(RCC->CFGR, RCC_CFGR_SW, RCC_CFGR_SW_PLL);
    if (!wait_flag(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_PLL)) {
        return;
    }
    f4_sysclk_hz = f4_pclk2_hz = PLL_VCO_HZ / 2u;
    f4_pclk1_hz = f4_sysclk_hz / 2u;
    SystemCoreClock = f4_sysclk_hz;
}

void f4_clock_deinit(void)
{
    SET_BIT(RCC->CR, RCC_CR_HSION);
    (void)wait_flag(&RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY);
    MODIFY_REG(RCC->CFGR, RCC_CFGR_SW, RCC_CFGR_SW_HSI);
    (void)wait_flag(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_HSI);
    RCC->CFGR = 0;                          /* prescalers /1, MCO off */
    CLEAR_BIT(RCC->CR, RCC_CR_PLLON);
    (void)wait_flag(&RCC->CR, RCC_CR_PLLRDY, 0);
    RCC->PLLCFGR = PLLCFGR_RESET;
    CLEAR_BIT(RCC->CR, RCC_CR_HSEON | RCC_CR_CSSON);
    (void)wait_flag(&RCC->CR, RCC_CR_HSERDY, 0);   /* HSEBYP is writable only with the HSE stopped */
    CLEAR_BIT(RCC->CR, RCC_CR_HSEBYP);
    RCC->CIR = 0x00BF0000u;                 /* clear ready/CSS flags, interrupts off */
    /* Regulator back to scale 2 (PWR_CR reset value); the PWR clock as found. */
    uint32_t pwren = RCC->APB1ENR & RCC_APB1ENR_PWREN;
    SET_BIT(RCC->APB1ENR, RCC_APB1ENR_PWREN);
    (void)RCC->APB1ENR;
    MODIFY_REG(PWR->CR, PWR_CR_VOS, PWR_CR_VOS_1);
    MODIFY_REG(RCC->APB1ENR, RCC_APB1ENR_PWREN, pwren);
    /* Zero wait states; caches off and flushed (reset value 0). */
    FLASH->ACR = 0;
    FLASH->ACR = FLASH_ACR_ICRST | FLASH_ACR_DCRST;
    FLASH->ACR = 0;
    SystemCoreClock = f4_sysclk_hz = f4_pclk1_hz = f4_pclk2_hz = HSI_HZ;
}
