/*
 * umcub example application, Blue Pill (STM32F103C8T6): the console of
 * examples/common/umcub_example.h on the bootloader's UART (its instance,
 * pins and baud rate from the configuration: USART1 PA9/PA10 by default,
 * USART3 PB10/PB11 in the RS485 variants), with its DE pin driven while
 * sending on RS485; LED PC13.
 *
 * Switches to the 8 MHz crystal: the HSI the bootloader leaves behind is only
 * +-1 % at 25 C, too little margin for 115200 baud.
 */
#include "stm32f1xx.h"
#include "umcub_cfg.h"
#include "umcub_example.h"

#if !UMCUB_CFG_TRANSPORT_UART            /* no UART in the bootloader: the console on USART1 PA9/PA10 */
#undef UMCUB_CFG_UART_INSTANCE
#undef UMCUB_CFG_UART_TX_PIN
#undef UMCUB_CFG_UART_RX_PIN
#undef UMCUB_CFG_UART_BAUD
#define UMCUB_CFG_UART_INSTANCE 1
#define UMCUB_CFG_UART_TX_PIN UMCUB_PIN('A', 9)
#define UMCUB_CFG_UART_RX_PIN UMCUB_PIN('A', 10)
#define UMCUB_CFG_UART_BAUD 115200
#undef UMCUB_CFG_UART_DE_PIN
#endif
/* The instance's default pins only (no AFIO remap): USART1 PA9/PA10, USART3 PB10/PB11. */
#if UMCUB_CFG_UART_INSTANCE == 1 && UMCUB_CFG_UART_TX_PIN == UMCUB_PIN('A', 9) && \
    UMCUB_CFG_UART_RX_PIN == UMCUB_PIN('A', 10)
#define CON USART1
#elif UMCUB_CFG_UART_INSTANCE == 3 && UMCUB_CFG_UART_TX_PIN == UMCUB_PIN('B', 10) && \
    UMCUB_CFG_UART_RX_PIN == UMCUB_PIN('B', 11)
#define CON USART3
#else
#error "bluepill_app: console on USART1 PA9/PA10 or USART3 PB10/PB11 only (no remapped pins)"
#endif

static GPIO_TypeDef *port_of(uint32_t pin)
{
    return (GPIO_TypeDef *)(GPIOA_BASE + UMCUB_PIN_PORT(pin) * (GPIOB_BASE - GPIOA_BASE));
}

/* F1 pin mode: 4 bits per pin in CRL (0-7) / CRH (8-15). */
static void pin_mode(uint32_t pin, uint32_t mode)
{
    volatile uint32_t *cr = UMCUB_PIN_NUM(pin) < 8 ? &port_of(pin)->CRL : &port_of(pin)->CRH;
    unsigned sh = (UMCUB_PIN_NUM(pin) & 7u) * 4u;
    *cr = (*cr & ~(0xFu << sh)) | (mode << sh);
}

static void de(bool transmit)
{
#ifdef UMCUB_CFG_UART_DE_PIN
    bool level = transmit == (UMCUB_CFG_UART_DE_ACTIVE != 0);
    port_of(UMCUB_CFG_UART_DE_PIN)->BSRR = (1u << UMCUB_PIN_NUM(UMCUB_CFG_UART_DE_PIN)) << (level ? 0 : 16);
#else
    (void)transmit;
#endif
}

static volatile uint32_t ms;
void SysTick_Handler(void) { ms++; }
uint32_t ex_millis(void) { return ms; }

void ex_putc(char c)
{
    de(true);
    CON->DR = (uint8_t)c;
    while (!(CON->SR & USART_SR_TC)) {
    }
    de(false);
}

bool ex_getc(uint8_t *c)
{
    uint32_t sr = CON->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) {
        *c = (uint8_t)CON->DR;          /* SR then DR read clears the error flags too */
        return (sr & USART_SR_RXNE) && !(sr & (USART_SR_FE | USART_SR_NE));     /* an error: noise, not a key */
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

    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPCEN |
                    (UMCUB_CFG_UART_INSTANCE == 1 ? RCC_APB2ENR_USART1EN : 0u);
    RCC->APB1ENR |= UMCUB_CFG_UART_INSTANCE == 3 ? RCC_APB1ENR_USART3EN : 0u;
    (void)RCC->APB2ENR;
    GPIOC->CRH = (GPIOC->CRH & ~(0xFu << 20)) | (0x2u << 20);      /* PC13 push-pull 2 MHz */
    /* SYSCLK = PCLK1 = PCLK2 = 8 MHz. */
    port_of(UMCUB_CFG_UART_RX_PIN)->BSRR = 1u << UMCUB_PIN_NUM(UMCUB_CFG_UART_RX_PIN);   /* pull-up: unconnected it reads noise */
    pin_mode(UMCUB_CFG_UART_TX_PIN, 0xBu);                          /* AF push-pull 50 MHz */
    pin_mode(UMCUB_CFG_UART_RX_PIN, 0x8u);                          /* input with pull */
#ifdef UMCUB_CFG_UART_DE_PIN
    de(false);
    pin_mode(UMCUB_CFG_UART_DE_PIN, 0x2u);                          /* push-pull 2 MHz: receive */
#endif
    CON->BRR = (SystemCoreClock + UMCUB_CFG_UART_BAUD / 2u) / UMCUB_CFG_UART_BAUD;
    CON->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    ex_run("Blue Pill");
}

/* Linked with -nostartfiles: __libc_init_array still calls these. */
void _init(void) {}
void _fini(void) {}
