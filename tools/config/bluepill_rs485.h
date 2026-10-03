/* Overlay (POST), Blue Pill: USART1 as RS485, DE driven by software on PB12
 * (F1 USARTs have no hardware DE). Build check; no log on a shared bus. */
#undef UMCUB_CFG_UART_DE_PIN
#define UMCUB_CFG_UART_DE_PIN           UMCUB_PIN('B', 12, 0)
#undef UMCUB_CFG_UART_TURNAROUND_MS
#define UMCUB_CFG_UART_TURNAROUND_MS    1
#undef UMCUB_CFG_LOG_LEVEL
#define UMCUB_CFG_LOG_LEVEL             0
