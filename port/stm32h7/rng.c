/*
 * STM32H7 port: RNG kernel clock for the RNG driver (port/arch/stm32/rng.c,
 * RM0399 §34): HSI48 (RNGSEL = 00 after reset).
 */
#include "h7.h"
#include "../arch/stm32/stm32_rng.h"

int stm32_rng_clock_on(void)
{
    if (h7_hsi48_on() != 0) {
        return UMCUB_EIO;
    }
    CLEAR_BIT(RCC->D2CCIP2R, RCC_D2CCIP2R_RNGSEL);     /* HSI48 */
    umcub_cm_periph_on(&RCC->AHB2ENR, &RCC->AHB2RSTR, RCC_AHB2ENR_RNGEN);
    return UMCUB_OK;
}
