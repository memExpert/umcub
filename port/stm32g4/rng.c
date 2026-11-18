/*
 * STM32G4 port: RNG kernel clock for the RNG driver (port/arch/stm32/rng.c,
 * RM0440 §26): HSI48 (CCIPR CLK48SEL = 00, the reset choice; g4_clock_deinit()
 * puts CCIPR back and stops the HSI48 before the jump).
 */
#include "g4.h"
#include "../arch/stm32/stm32_rng.h"

int stm32_rng_clock_on(void)
{
    SET_BIT(RCC->CRRCR, RCC_CRRCR_HSI48ON);
    if (umcub_cm_wait(&RCC->CRRCR, RCC_CRRCR_HSI48RDY, RCC_CRRCR_HSI48RDY, 10) != 0) {
        return UMCUB_EIO;
    }
    umcub_cm_periph_on(&RCC->AHB2ENR, &RCC->AHB2RSTR, RCC_AHB2ENR_RNGEN);
    return UMCUB_OK;
}
