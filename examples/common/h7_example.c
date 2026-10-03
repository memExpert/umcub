#include "h7_example.h"
#include "stm32h7xx.h"

static GPIO_TypeDef *port_of(char port)
{
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOAEN << (port - 'A');
    (void)RCC->AHB4ENR;
    return (GPIO_TypeDef *)(GPIOA_BASE + (uint32_t)(port - 'A') * 0x400u);
}

void ex_led_init(char port, int pin)
{
    GPIO_TypeDef *g = port_of(port);
    g->MODER = (g->MODER & ~(3u << (pin * 2))) | (1u << (pin * 2));
}

void ex_led_toggle(char port, int pin)
{
    GPIO_TypeDef *g = port_of(port);
    g->ODR ^= 1u << pin;
}

void ex_delay_ms(uint32_t ms)
{
    /* SysTick as a down-counter, no interrupt. */
    SysTick->LOAD = SystemCoreClock / 1000u - 1u;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    while (ms--) {
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk)) {
        }
    }
    SysTick->CTRL = 0;
}

void ex_uart_init(void)
{
    GPIO_TypeDef *d = port_of('D');
    /* PD8 TX, PD9 RX, AF7 */
    d->MODER = (d->MODER & ~(0xFu << 16)) | (0xAu << 16);
    d->AFR[1] = (d->AFR[1] & ~0xFFu) | 0x77u;
    RCC->APB1LENR |= RCC_APB1LENR_USART3EN;
    (void)RCC->APB1LENR;
    USART3->CR1 = 0;
    USART3->BRR = (64000000u + 115200u / 2u) / 115200u;   /* PCLK1 = HSI 64 MHz after reset */
    /* 16-byte RX FIFO: the main loop polls only once per millisecond. */
    USART3->CR1 = USART_CR1_FIFOEN | USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

bool ex_uart_getc(uint8_t *c)
{
    if (USART3->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
        USART3->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
    }
    if (USART3->ISR & USART_ISR_RXNE_RXFNE) {
        *c = (uint8_t)USART3->RDR;
        return true;
    }
    return false;
}

void ex_uart_putc(char c)
{
    while (!(USART3->ISR & USART_ISR_TXE_TXFNF)) {
    }
    USART3->TDR = (uint8_t)c;
}

void ex_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            ex_uart_putc('\r');
        }
        ex_uart_putc(*s++);
    }
}

void ex_put_u32(uint32_t v)
{
    char b[11];
    int i = 10;
    b[i] = 0;
    do {
        b[--i] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    ex_puts(&b[i]);
}

void ex_put_hex(uint32_t v)
{
    ex_puts("0x");
    for (int s = 28; s >= 0; s -= 4) {
        ex_uart_putc("0123456789abcdef"[(v >> s) & 0xFu]);
    }
}

/* Linked with -nostartfiles: __libc_init_array still calls these. */
void _init(void) {}
void _fini(void) {}
