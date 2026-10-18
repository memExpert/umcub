/*
 * RNG driver shared by the family ports (port/arch/stm32/rng.c implements
 * umcub_port_entropy()). The family supplies the clock.
 */
#ifndef UMCUB_STM32_RNG_H
#define UMCUB_STM32_RNG_H

#include "umcub_family_cmsis.h"

/* Family: the RNG kernel clock running and selected, the RNG clocked (RCC
 * enable + reset bookkeeping); UMCUB_OK or an error (clock does not start). */
int stm32_rng_clock_on(void);

#endif
