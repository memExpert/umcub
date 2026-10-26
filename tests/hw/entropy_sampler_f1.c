/*
 * Entropy sampler for STM32F103, loaded into SRAM by tests/hw/entropy_test.py
 * (nothing is written to flash). It runs the bootloader's conditions: the clock
 * setup of port/stm32f1/clock.c (HSE 8 MHz x9 = 72 MHz, APB1 /2), SysTick 1 ms
 * with its interrupt, ADC1 as in port/stm32f1/entropy.c (ADCPRE /6, temperature
 * sensor on channel 16 at 239.5 cycles, one conversion per ADON, the same wait
 * for EOC), and records every raw conversion plus the low byte of SysTick->VAL
 * read after it (to check that SysTick adds nothing). Keep it in step with
 * entropy.c. The host reads rounds of N samples over SWD.
 */
#include "stm32f1xx.h"
#define N 4096u
struct shared {
    uint32_t magic;             /* 0x454E5452 'ENTR' when running */
    volatile uint32_t ready;    /* 1: buffer full, host clears it */
    volatile uint32_t rounds;
    volatile uint32_t ms;
    uint32_t timeouts;
    uint16_t v[N];
    uint8_t st[N];
};
__attribute__((section(".shared"))) struct shared sh;
extern uint32_t _estack;
void reset(void);
static void systick(void) { sh.ms++; }
static void dflt(void) { for (;;) {} }
__attribute__((section(".vectors"), used)) void (*const vectors[16])(void) = {
    (void (*)(void))&_estack, reset, dflt, dflt, dflt, dflt, dflt, 0, 0, 0, 0, dflt, dflt, 0, dflt, systick,
};
static int wait_eoc(void)       /* like umcub_cm_wait(&ADC1->SR, EOC, EOC, 2) */
{
    uint32_t start = sh.ms;
    while ((ADC1->SR & ADC_SR_EOC) != ADC_SR_EOC) {
        if ((uint32_t)(sh.ms - start) > 2u) {
            return -1;
        }
    }
    return 0;
}
void reset(void)
{
    SCB->VTOR = 0x20000000u;
    sh.magic = 0; sh.ready = 0; sh.rounds = 0; sh.ms = 0; sh.timeouts = 0;
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {}
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL)) | RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9;
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_1;
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2)) | RCC_CFGR_PPRE1_DIV2;
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
    SysTick->LOAD = 72000000u / 1000u - 1u;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
    __enable_irq();
    /* adc_start() */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_ADCPRE) | RCC_CFGR_ADCPRE_DIV6;
    RCC->APB2RSTR |= RCC_APB2RSTR_ADC1RST; RCC->APB2RSTR &= ~RCC_APB2RSTR_ADC1RST;
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    ADC1->SMPR1 = ADC_SMPR1_SMP16;
    ADC1->SQR1 = 0;
    ADC1->SQR3 = 16;
    ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_TSVREFE;
    uint32_t t = sh.ms; while ((uint32_t)(sh.ms - t) < 2u) {}       /* umcub_port_delay_ms(1) */
    sh.magic = 0x454E5452u;
    for (;;) {
        for (uint32_t i = 0; i < N; i++) {
            ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_TSVREFE;
            if (wait_eoc()) { sh.timeouts++; }
            sh.v[i] = (uint16_t)ADC1->DR;
            sh.st[i] = (uint8_t)(SysTick->VAL & 0xFFu);
        }
        sh.rounds++;
        sh.ready = 1;
        while (sh.ready) {}
    }
}
