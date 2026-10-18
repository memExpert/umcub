/*
 * Entropy (umcub_port_entropy) from the true random number generator, for
 * every family with the RNG peripheral (H7, G4, L4, F4, F7, H5, U5); the
 * family switches its kernel clock on (stm32_rng_clock_on()).
 */
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "stm32_rng.h"
#include "stm32_common.h"

static bool rng_up;

static int rng_start(void)
{
    if (stm32_rng_clock_on() != UMCUB_OK) {
        return UMCUB_EIO;
    }
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
            /* Seed error (RM0399 §36.3.7, the same on every RNG): clear SEIS, discard 12 words, check
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
