/*
 * STM32H7 power and clock tree (RM0399 §6 PWR, §8 RCC).
 *
 * Bootloader clock plan (main core only):
 *   PLL1 ref = 2 MHz, VCO = 800 MHz, P = 400 MHz (SYSCLK), Q = 100 MHz (FDCAN)
 *   HCLK = 200 MHz, all APB = 100 MHz, VOS1, flash 2 WS.
 *   USB from HSI48 (enabled by the USB driver), ETH from HCLK.
 * On the CM4 side nothing is configured here: the CM7 owns the clock tree.
 */
#include "h7.h"

uint32_t h7_sysclk_hz = 64000000;
uint32_t h7_hclk_hz = 64000000;
uint32_t h7_pclk_hz = 64000000;
uint32_t h7_pll1q_hz = 0;

uint32_t SystemCoreClock = 64000000;

#define PLL_REF_HZ   2000000u
#define PLL_N        400u
#define PLL_P        2u
#define PLL_Q        8u

#if UMCUB_CFG_CLOCK_SOURCE == UMCUB_CLK_HSE
#define PLL_SRC_HZ   UMCUB_CFG_HSE_HZ
#else
#define PLL_SRC_HZ   64000000u
#endif
#if (PLL_SRC_HZ % PLL_REF_HZ) != 0 || (PLL_SRC_HZ / PLL_REF_HZ) > 63
#error "umcub/h7: clock source must be a multiple of 2 MHz (<= 126 MHz)"
#endif

static uint32_t supply_bits(void)
{
#if UMCUB_CFG_PWR_SUPPLY == UMCUB_H7_SUPPLY_LDO
    return LL_PWR_LDO_SUPPLY;
#elif UMCUB_CFG_PWR_SUPPLY == UMCUB_H7_SUPPLY_EXTERNAL
    return LL_PWR_EXTERNAL_SOURCE_SUPPLY;
#elif defined(SMPS) && UMCUB_CFG_PWR_SUPPLY == UMCUB_H7_SUPPLY_DIRECT_SMPS
    return LL_PWR_DIRECT_SMPS_SUPPLY;
#elif defined(SMPS) && UMCUB_CFG_PWR_SUPPLY == UMCUB_H7_SUPPLY_SMPS_1V8_LDO
    return LL_PWR_SMPS_1V8_SUPPLIES_LDO;
#elif defined(SMPS) && UMCUB_CFG_PWR_SUPPLY == UMCUB_H7_SUPPLY_SMPS_2V5_LDO
    return LL_PWR_SMPS_2V5_SUPPLIES_LDO;
#elif defined(SMPS) && UMCUB_CFG_PWR_SUPPLY == UMCUB_H7_SUPPLY_SMPS_1V8_EXT_LDO
    return LL_PWR_SMPS_1V8_SUPPLIES_EXT_AND_LDO;
#elif defined(SMPS) && UMCUB_CFG_PWR_SUPPLY == UMCUB_H7_SUPPLY_SMPS_2V5_EXT_LDO
    return LL_PWR_SMPS_2V5_SUPPLIES_EXT_AND_LDO;
#elif defined(SMPS) && UMCUB_CFG_PWR_SUPPLY == UMCUB_H7_SUPPLY_SMPS_1V8_EXT
    return LL_PWR_SMPS_1V8_SUPPLIES_EXT;
#elif defined(SMPS) && UMCUB_CFG_PWR_SUPPLY == UMCUB_H7_SUPPLY_SMPS_2V5_EXT
    return LL_PWR_SMPS_2V5_SUPPLIES_EXT;
#else
#error "umcub/h7: UMCUB_CFG_PWR_SUPPLY not supported by this device"
#endif
}

/* Before SysTick runs: crude cycle-count timeout (HSI 64 MHz, generous). */
static int spin_until(volatile uint32_t *reg, uint32_t mask, uint32_t value)
{
    for (uint32_t n = 0; n < 20000000u; n++) {
        if ((*reg & mask) == value) {
            return 0;
        }
    }
    return UMCUB_ETIMEOUT;
}

void h7_clock_init(void)
{
    /* PWR_CR3 supply configuration is write-once after power-on (RM0399
     * §6.8.4): writing the same value again is harmless. */
    LL_PWR_ConfigSupply(supply_bits());
    (void)spin_until(&PWR->CSR1, PWR_CSR1_ACTVOSRDY, PWR_CSR1_ACTVOSRDY);

    LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);
    (void)spin_until(&PWR->D3CR, PWR_D3CR_VOSRDY, PWR_D3CR_VOSRDY);

#if UMCUB_CFG_CLOCK_SOURCE == UMCUB_CLK_HSE
#if UMCUB_CFG_HSE_BYPASS
    LL_RCC_HSE_EnableBypass();
#endif
    LL_RCC_HSE_Enable();
    if (spin_until(&RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY) != 0) {
        return; /* stay on HSI 64 MHz: slow but working */
    }
    LL_RCC_PLL_SetSource(LL_RCC_PLLSOURCE_HSE);
#else
    LL_RCC_PLL_SetSource(LL_RCC_PLLSOURCE_HSI);
#endif

    LL_RCC_PLL1P_Enable();
    LL_RCC_PLL1Q_Enable();
    LL_RCC_PLL1_SetVCOInputRange(LL_RCC_PLLINPUTRANGE_2_4);
    LL_RCC_PLL1_SetVCOOutputRange(LL_RCC_PLLVCORANGE_WIDE);
    LL_RCC_PLL1_SetM(PLL_SRC_HZ / PLL_REF_HZ);
    LL_RCC_PLL1_SetN(PLL_N);
    LL_RCC_PLL1_SetP(PLL_P);
    LL_RCC_PLL1_SetQ(PLL_Q);
    LL_RCC_PLL1_SetR(2);
    LL_RCC_PLL1_Enable();
    if (spin_until(&RCC->CR, RCC_CR_PLL1RDY, RCC_CR_PLL1RDY) != 0) {
        return;
    }

    /* 200 MHz AXI at VOS1: 2 wait states, WRHIGHFREQ = 2 (RM0399 Table 17). */
    MODIFY_REG(FLASH->ACR, FLASH_ACR_LATENCY | FLASH_ACR_WRHIGHFREQ,
               FLASH_ACR_LATENCY_2WS | (2u << FLASH_ACR_WRHIGHFREQ_Pos));

    LL_RCC_SetSysPrescaler(LL_RCC_SYSCLK_DIV_1);
    LL_RCC_SetAHBPrescaler(LL_RCC_AHB_DIV_2);
    LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_2);
    LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_2);
    LL_RCC_SetAPB3Prescaler(LL_RCC_APB3_DIV_2);
    LL_RCC_SetAPB4Prescaler(LL_RCC_APB4_DIV_2);

    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL1);
    (void)spin_until(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_PLL1);

    h7_sysclk_hz = PLL_REF_HZ * PLL_N / PLL_P;
    h7_hclk_hz = h7_sysclk_hz / 2;
    h7_pclk_hz = h7_hclk_hz / 2;
    h7_pll1q_hz = PLL_REF_HZ * PLL_N / PLL_Q;
    SystemCoreClock = h7_sysclk_hz;
}

void h7_clock_deinit(void)
{
    /* Back to the reset clock tree: HSI 64 MHz, no prescalers, PLLs off.
     * Voltage scaling and flash latency are left as they are (higher than
     * needed is safe; the application programs its own values). */
    LL_RCC_HSI_Enable();
    (void)spin_until(&RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY);
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSI);
    (void)spin_until(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_HSI);

    RCC->D1CFGR = 0;
    RCC->D2CFGR = 0;
    RCC->D3CFGR = 0;
    CLEAR_BIT(RCC->CR, RCC_CR_PLL1ON | RCC_CR_PLL2ON | RCC_CR_PLL3ON);
    (void)spin_until(&RCC->CR, RCC_CR_PLL1RDY | RCC_CR_PLL2RDY | RCC_CR_PLL3RDY, 0);
    CLEAR_BIT(RCC->CR, RCC_CR_HSEON | RCC_CR_HSI48ON | RCC_CR_CSION);
    CLEAR_BIT(RCC->CR, RCC_CR_HSEBYP);
    RCC->PLLCKSELR = 0x02020200u;   /* reset values (RM0399 §8.7) */
    RCC->PLLCFGR = 0x01FF0000u;
    RCC->PLL1DIVR = 0x01010280u;
    RCC->PLL1FRACR = 0;
    RCC->D1CCIPR = 0;
    RCC->D2CCIP1R = 0;
    RCC->D2CCIP2R = 0;
    RCC->D3CCIPR = 0;
    RCC->CIER = 0;
    RCC->CICR = 0xFFFFFFFFu;

    h7_sysclk_hz = h7_hclk_hz = h7_pclk_hz = 64000000;
    h7_pll1q_hz = 0;
    SystemCoreClock = 64000000;
}

int h7_hsi48_on(void)
{
    LL_RCC_HSI48_Enable();
    return umcub_cm_wait(&RCC->CR, RCC_CR_HSI48RDY, RCC_CR_HSI48RDY, 10) == 0 ? UMCUB_OK : UMCUB_EIO;
}
