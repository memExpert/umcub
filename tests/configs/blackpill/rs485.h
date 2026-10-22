/* Overlay (POST), Black Pill F411: USART1 PA9/PA10 as RS485 through a MAX485,
 * DE (with /RE) driven by software on PA8 (F4 USARTs have no hardware DE).
 * No log on a shared bus. */
#undef UMCUB_CFG_UART_DE_PIN
#define UMCUB_CFG_UART_DE_PIN           UMCUB_PIN('A', 8, 0)
#undef UMCUB_CFG_UART_DE_ACTIVE
#define UMCUB_CFG_UART_DE_ACTIVE        1
#undef UMCUB_CFG_UART_TURNAROUND_MS
#define UMCUB_CFG_UART_TURNAROUND_MS    1
#undef UMCUB_CFG_LOG_LEVEL
#define UMCUB_CFG_LOG_LEVEL             0
