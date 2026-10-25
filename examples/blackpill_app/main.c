/*
 * umcub example application, Black Pill (STM32F411CEU6): the console of
 * examples/common/umcub_example.h on USART1 PA9/PA10 (115200), the
 * bootloader's port; LED PC13. PA8 is driven high while sending (DE of an
 * RS485 transceiver, no effect on a plain UART).
 *
 * Starts on the reset clock the bootloader leaves behind (HSI 16 MHz) and
 * switches to the 25 MHz crystal: HSI is only +-4 % over temperature (DS10314,
 * more after soldering), too far off for 115200 baud.
 */
#include "stm32f4xx.h"
#include "umcub_example.h"

static volatile uint32_t ms;
void SysTick_Handler(void) { ms++; }
uint32_t ex_millis(void) { return ms; }

void ex_putc(char c)
{
    GPIOA->BSRR = 1u << 8;                  /* DE: transmit */
    USART1->DR = (uint8_t)c;
    while (!(USART1->SR & USART_SR_TC)) {
    }
    GPIOA->BSRR = 1u << (8 + 16);           /* DE: receive */
}

bool ex_getc(uint8_t *c)
{
    uint32_t sr = USART1->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
        *c = (uint8_t)USART1->DR;
        return (sr & USART_SR_RXNE) != 0;
    }
    return false;
}

void ex_led_toggle(void) { GPIOC->ODR ^= 1u << 13; }
void ex_watchdog_feed(void) { IWDG->KR = 0xAAAAu; }

int main(void)
{
    /* HSE 25 MHz as SYSCLK (no PLL, 0 wait states up to 30 MHz); stays on
     * HSI 16 MHz, the reset clock the bootloader restored, without a crystal. */
    SystemCoreClock = 16000000u;
    RCC->CR |= RCC_CR_HSEON;
    for (uint32_t n = 0; n < 500000u; n++) {
        if (RCC->CR & RCC_CR_HSERDY) {
            RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSE;
            while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSE) {
            }
            SystemCoreClock = 25000000u;
            break;
        }
    }
    SysTick_Config(SystemCoreClock / 1000u);

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;
    GPIOC->MODER = (GPIOC->MODER & ~(3u << 26)) | (1u << 26);       /* PC13 output */
    GPIOA->BSRR = 1u << (8 + 16);
    GPIOA->MODER = (GPIOA->MODER & ~(0x3Fu << 16)) | (0x29u << 16); /* PA8 output, PA9/PA10 AF */
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~0xFF0u) | 0x770u;              /* PA9/PA10 AF7 = USART1 */
    GPIOA->PUPDR = (GPIOA->PUPDR & ~(3u << 20)) | (1u << 20);       /* PA10 pull-up */
    USART1->BRR = (SystemCoreClock + 115200u / 2u) / 115200u;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    ex_run("Black Pill F411");
}

/* Linked with -nostartfiles: __libc_init_array still calls these. */
void _init(void) {}
void _fini(void) {}
