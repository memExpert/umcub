/*
 * STM32F1 port: entropy without a TRNG. Each output byte collects the noisy
 * low bits of several ADC1 conversions of the internal temperature sensor
 * (RM0008 §11.10) mixed with the SysTick counter (conversion time jitter
 * against the CPU clock). The caller conditions it with a hash; the quality is
 * lower than a true RNG and documented as such.
 */
#include "f1.h"

static bool adc_up;

static void adc_start(void)
{
    MODIFY_REG(RCC->CFGR, RCC_CFGR_ADCPRE, RCC_CFGR_ADCPRE_DIV6);   /* <= 14 MHz at 72 MHz */
    SET_BIT(RCC->APB2ENR, RCC_APB2ENR_ADC1EN);
    (void)RCC->APB2ENR;
    f1_periph_used(&RCC->APB2RSTR, RCC_APB2RSTR_ADC1RST);
    ADC1->SMPR1 = 0;                                    /* channel 16: 1.5 cycles, the noisiest */
    ADC1->SQR1 = 0;                                     /* one conversion */
    ADC1->SQR3 = 16;                                    /* temperature sensor */
    ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_TSVREFE;         /* power up */
    umcub_port_delay_ms(1);                             /* tSTAB (1 us) and sensor start-up */
    adc_up = true;
}

static int sample(uint16_t *v)
{
    ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_TSVREFE;         /* ADON again: start a conversion */
    if (f1_wait(&ADC1->SR, ADC_SR_EOC, ADC_SR_EOC, 2) != 0) {
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
            b = (uint8_t)((b << 2) ^ (v & 0x3u) ^ (SysTick->VAL & 0xFFu));
        }
        *buf++ = b;
    }
    return UMCUB_OK;
}
