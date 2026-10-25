/*
 * umcub example application, Blue Pill (STM32F103C8T6): the console of
 * examples/common/umcub_example.h on USART1 PA9/PA10 (115200), the
 * bootloader's port; LED PC13.
 *
 * Switches to the 8 MHz crystal: the HSI the bootloader leaves behind is only
 * +-1 % at 25 C, too little margin for 115200 baud.
 */
#include "stm32f1xx.h"
#include "umcub_example.h"

static volatile uint32_t ms;
void SysTick_Handler(void) { ms++; }
uint32_t ex_millis(void) { return ms; }

void ex_putc(char c)
{
    while (!(USART1->SR & USART_SR_TXE)) {
    }
    USART1->DR = (uint8_t)c;
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
    /* HSE 8 MHz as SYSCLK (no PLL, 0 wait states up to 24 MHz); stays on
     * HSI 8 MHz, the reset clock the bootloader restored, without a crystal.
     * Same frequency either way: only the accuracy changes. */
    SystemCoreClock = 8000000u;
    RCC->CR |= RCC_CR_HSEON;
    for (uint32_t n = 0; n < 500000u; n++) {
        if (RCC->CR & RCC_CR_HSERDY) {
            RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSE;
            while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSE) {
            }
            break;
        }
    }
    SysTick_Config(SystemCoreClock / 1000u);

    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPCEN | RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;
    GPIOC->CRH = (GPIOC->CRH & ~(0xFu << 20)) | (0x2u << 20);      /* PC13 push-pull 2 MHz */
    GPIOA->CRH = (GPIOA->CRH & ~(0xFFu << 4)) | (0x4Bu << 4);      /* PA9 AF PP 50 MHz, PA10 input */
    USART1->BRR = (SystemCoreClock + 115200u / 2u) / 115200u;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    ex_run("Blue Pill");
}

/* Linked with -nostartfiles: __libc_init_array still calls these. */
void _init(void) {}
void _fini(void) {}
