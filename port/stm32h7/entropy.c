/*
 * STM32H7 port: entropy from the true random number generator (RM0399 §34
 * "RNG"), kernel clock HSI48 (RNGSEL = 00 after reset).
 */
#include "h7.h"

static bool rng_up;

static int rng_start(void)
{
    SET_BIT(RCC->CR, RCC_CR_HSI48ON);
    if (h7_wait(&RCC->CR, RCC_CR_HSI48RDY, RCC_CR_HSI48RDY, 10) != 0) {
        return UMCUB_EIO;
    }
    CLEAR_BIT(RCC->D2CCIP2R, RCC_D2CCIP2R_RNGSEL);     /* HSI48 */
    SET_BIT(RCC->AHB2ENR, RCC_AHB2ENR_RNGEN);
    (void)RCC->AHB2ENR;
    h7_periph_used(&RCC->AHB2RSTR, RCC_AHB2RSTR_RNGRST);
    RNG->CR = RNG_CR_RNGEN;                             /* clock error detection on (CED = 0) */
    rng_up = true;
    return UMCUB_OK;
}

int umcub_port_entropy(uint8_t *buf, size_t len)
{
    if (!rng_up && rng_start() != UMCUB_OK) {
        return UMCUB_EIO;
    }
    while (len) {
        if (RNG->SR & (RNG_SR_SEIS | RNG_SR_CEIS)) {
            /* Seed error: clear and restart the generator (RM0399 RNG error management). */
            RNG->SR = 0;
            RNG->CR = 0;
            RNG->CR = RNG_CR_RNGEN;
        }
        if (h7_wait(&RNG->SR, RNG_SR_DRDY, RNG_SR_DRDY, 10) != 0) {
            return UMCUB_ETIMEOUT;
        }
        uint32_t v = RNG->DR;
        for (unsigned i = 0; i < 4 && len; i++, len--) {
            *buf++ = (uint8_t)(v >> (8 * i));
        }
    }
    return UMCUB_OK;
}
