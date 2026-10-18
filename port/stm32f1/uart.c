/*
 * STM32F1 port: USART1..3 for the USART v1 driver (port/arch/stm32/usart_v1.c,
 * RM0008 §27). Pins: the default mapping or the AFIO remap the pins select
 * (RM0008 §9.3.8): USART1 PA9/PA10 or PB6/PB7, USART2 PA2/PA3 or PD5/PD6,
 * USART3 PB10/PB11, PC10/PC11 (partial) or PD8/PD9 (full). Remapped pins
 * exist only on bigger packages (RM0008 Tables 52/53).
 */
#include "f1.h"
#include "../arch/stm32/stm32_usart.h"

#define PIN(port, n) UMCUB_PIN(port, n, 0)
#define SAME_PIN(a, b) (((a) & ~0xFu) == ((b) & ~0xFu))   /* ignore the AF field */

static const stm32_usart_t usarts[] = {
    { USART1, &RCC->APB2ENR, &RCC->APB2RSTR, RCC_APB2ENR_USART1EN, &f1_pclk2_hz, USART1_IRQn },
    { USART2, &RCC->APB1ENR, &RCC->APB1RSTR, RCC_APB1ENR_USART2EN, &f1_pclk1_hz, USART2_IRQn },
    { USART3, &RCC->APB1ENR, &RCC->APB1RSTR, RCC_APB1ENR_USART3EN, &f1_pclk1_hz, USART3_IRQn },
};

/* instance, TX, RX, AFIO_MAPR field and value: the table below and the
 * build-time check of the configured pins */
#define MAPS(X)                                                                                         \
    X(1, PIN('A', 9), PIN('A', 10), AFIO_MAPR_USART1_REMAP, 0)                                          \
    X(1, PIN('B', 6), PIN('B', 7), AFIO_MAPR_USART1_REMAP, AFIO_MAPR_USART1_REMAP)                      \
    X(2, PIN('A', 2), PIN('A', 3), AFIO_MAPR_USART2_REMAP, 0)                                           \
    X(2, PIN('D', 5), PIN('D', 6), AFIO_MAPR_USART2_REMAP, AFIO_MAPR_USART2_REMAP)                      \
    X(3, PIN('B', 10), PIN('B', 11), AFIO_MAPR_USART3_REMAP, 0)                                         \
    X(3, PIN('C', 10), PIN('C', 11), AFIO_MAPR_USART3_REMAP, AFIO_MAPR_USART3_REMAP_PARTIALREMAP)       \
    X(3, PIN('D', 8), PIN('D', 9), AFIO_MAPR_USART3_REMAP, AFIO_MAPR_USART3_REMAP_FULLREMAP)

#define CONFIGURED(i, tx, rx, mask, val)                                                                \
    (UMCUB_CFG_UART_INSTANCE == (i) && SAME_PIN(UMCUB_CFG_UART_TX_PIN, tx) && SAME_PIN(UMCUB_CFG_UART_RX_PIN, rx)) ||
_Static_assert(MAPS(CONFIGURED) 0,
               "umcub: UMCUB_CFG_UART_TX_PIN / _RX_PIN are not a pin pair of UMCUB_CFG_UART_INSTANCE (STM32F1: "
               "USART1 PA9/PA10 or PB6/PB7, USART2 PA2/PA3 or PD5/PD6, USART3 PB10/PB11, PC10/PC11 or PD8/PD9)");

#define ENTRY(i, tx, rx, mask, val) { i, tx, rx, mask, val },
static const struct {
    uint8_t instance;
    uint32_t tx, rx;
    uint32_t remap_mask, remap_val;   /* AFIO_MAPR */
} maps[] = { MAPS(ENTRY) };

const stm32_usart_t *stm32_usart_get(const umcub_uart_cfg_t *cfg)
{
    for (unsigned i = 0; i < sizeof(maps) / sizeof(maps[0]); i++) {
        if (maps[i].instance != cfg->instance || !SAME_PIN(maps[i].tx, cfg->tx_pin) ||
            !SAME_PIN(maps[i].rx, cfg->rx_pin)) {
            continue;
        }
        if (maps[i].remap_val) {
            umcub_cm_periph_on(&RCC->APB2ENR, &RCC->APB2RSTR, RCC_APB2ENR_AFIOEN);
            /* SWJ_CFG is write-only and reads back undefined (RM0008 §9.4.2): write
             * 111 ("no effect") with the remap bits, or SWD may get switched off. */
            MODIFY_REG(AFIO->MAPR, maps[i].remap_mask | AFIO_MAPR_SWJ_CFG, maps[i].remap_val | AFIO_MAPR_SWJ_CFG);
        }
        f1_gpio_config(cfg->tx_pin, F1_GPIO_AF_PP_50M);
        umcub_port_gpio_input(cfg->rx_pin, UMCUB_PULL_UP);   /* idle high if unconnected */
        return &usarts[cfg->instance - 1u];
    }
    return NULL;
}

void USART1_IRQHandler(void) { stm32_usart_isr(); }
void USART2_IRQHandler(void) { stm32_usart_isr(); }
void USART3_IRQHandler(void) { stm32_usart_isr(); }
