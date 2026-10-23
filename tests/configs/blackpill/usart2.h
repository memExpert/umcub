/* Overlay (POST), Black Pill F411: SMP and log on USART2 PA2 (TX) / PA3 (RX),
 * AF7 - e.g. a USB-UART adapter, with USART1 PA9/PA10 left to something else. */
#undef UMCUB_CFG_UART_INSTANCE
#define UMCUB_CFG_UART_INSTANCE         2
#undef UMCUB_CFG_UART_TX_PIN
#define UMCUB_CFG_UART_TX_PIN           UMCUB_PIN('A', 2, 7)
#undef UMCUB_CFG_UART_RX_PIN
#define UMCUB_CFG_UART_RX_PIN           UMCUB_PIN('A', 3, 7)
