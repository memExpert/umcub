/*
 * STM32H7 port: entropy from the true random number generator (RM0399 §34
 * "RNG"), kernel clock HSI48 (RNGSEL = 00 after reset).
 */
#include "h7.h"

static bool rng_up;

static int rng_start(void)
{
    if (h7_hsi48_on() != 0) {
        return UMCUB_EIO;
    }
    CLEAR_BIT(RCC->D2CCIP2R, RCC_D2CCIP2R_RNGSEL);     /* HSI48 */
    SET_BIT(RCC->AHB2ENR, RCC_AHB2ENR_RNGEN);
    (void)RCC->AHB2ENR;
    umcub_cm_periph_used(&RCC->AHB2RSTR, RCC_AHB2RSTR_RNGRST);
    RNG->CR = RNG_CR_RNGEN;                             /* clock error detection on (CED = 0) */
    rng_up = true;
    return UMCUB_OK;
}

int umcub_port_entropy(uint8_t *buf, size_t len)
{
    if (!rng_up && rng_start() != UMCUB_OK) {
        return UMCUB_EIO;
    }
    unsigned recoveries = 0;
    while (len) {
        if (RNG->SR & RNG_SR_SEIS) {
            /* Seed error (RM0399 §36.3.7): clear SEIS, discard 12 words, check
             * SEIS again. Bounded: a generator that keeps failing is an error. */
            if (++recoveries > 4) {
                return UMCUB_EIO;
            }
            RNG->SR = 0;                            /* rc_w0 */
            for (unsigned k = 0; k < 12; k++) {
                (void)RNG->DR;
            }
            continue;
        }
        if (RNG->SR & RNG_SR_CEIS) {
            RNG->SR = 0;                            /* clock error: no impact on the numbers (§36.3.7) */
        }
        if (umcub_cm_wait(&RNG->SR, RNG_SR_DRDY, RNG_SR_DRDY, 10) != 0) {
            return UMCUB_ETIMEOUT;
        }
        uint32_t v = RNG->DR;
        if (v == 0 || (RNG->SR & RNG_SR_SEIS)) {
            continue;                               /* §36.3.5: 0 = seed error between SR and DR */
        }
        for (unsigned i = 0; i < 4 && len; i++, len--) {
            *buf++ = (uint8_t)(v >> (8 * i));
        }
    }
    return UMCUB_OK;
}
