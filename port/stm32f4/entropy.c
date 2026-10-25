/*
 * STM32F4 port: entropy without a TRNG (F411: no RNG). Each output byte is the
 * two low bits of four ADC1 conversions of the internal temperature sensor
 * (RM0383 §11.10, ADC1_IN18): their noise is the only source, like on the F1
 * (port/stm32f1/entropy.c). Measure a board with tools/hw/entropy_test.py
 * --family f4. The caller conditions it with a hash (src/util/random.c).
 */
#include "f4.h"

static bool adc_up;

static void adc_start(void)
{
    umcub_cm_periph_on(&RCC->APB2ENR, &RCC->APB2RSTR, RCC_APB2ENR_ADC1EN);
    /* PCLK2 / 4: 24 MHz at 96 MHz (DS10314: fADC <= 36 MHz); sensor on. */
    ADC->CCR = ADC_CCR_ADCPRE_0 | ADC_CCR_TSVREFE;
    /* Channel 18 at 480 cycles (20 us; DS10314: TS_temp >= 10 us). The
     * conversion value itself is not used, only its noisy low bits; their
     * quality is not specified by ST, hence the hash conditioning. */
    ADC1->SMPR1 = ADC_SMPR1_SMP18;
    ADC1->SQR1 = 0;                                     /* one conversion */
    ADC1->SQR3 = 18;                                    /* temperature sensor */
    ADC1->CR2 = ADC_CR2_ADON;                           /* power up */
    umcub_port_delay_ms(1);                             /* tSTAB and sensor start-up (10 us) */
    adc_up = true;
}

static int sample(uint16_t *v)
{
    ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_SWSTART;
    if (umcub_cm_wait(&ADC1->SR, ADC_SR_EOC, ADC_SR_EOC, 2) != 0) {
        return UMCUB_ETIMEOUT;
    }
    *v = (uint16_t)ADC1->DR;                            /* read clears EOC */
    return UMCUB_OK;
}

int umcub_port_entropy(uint8_t *buf, size_t len)
{
    if (!adc_up) {
        adc_start();
    }
    while (len--) {
        uint8_t b = 0;
        for (unsigned i = 0; i < 4; i++) {
            uint16_t v;
            if (sample(&v) != UMCUB_OK) {
                return UMCUB_ETIMEOUT;
            }
            b = (uint8_t)(b << 2 | (v & 0x3u));
        }
        *buf++ = b;
    }
    return UMCUB_OK;
}
