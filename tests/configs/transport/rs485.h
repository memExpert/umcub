/* Overlay (POST), NUCLEO-H755ZI-Q: USART3 as RS485 with hardware DE on PD12
 * (USART3_RTS/DE, AF7). Build check of the DE path; no log on a shared bus. */
#undef UMCUB_CFG_UART_DE_PIN
#define UMCUB_CFG_UART_DE_PIN           UMCUB_PIN('D', 12, 7)
#undef UMCUB_CFG_UART_TURNAROUND_MS
#define UMCUB_CFG_UART_TURNAROUND_MS    1
#undef UMCUB_CFG_LOG_LEVEL
#define UMCUB_CFG_LOG_LEVEL             0
