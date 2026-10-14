/* Overlay (POST), Blue Pill: USART3 on PB10/PB11 as RS485, DE driven by
 * software on PB12 (F1 USARTs have no hardware DE). No log on a shared bus. */
#undef UMCUB_CFG_UART_INSTANCE
#define UMCUB_CFG_UART_INSTANCE         3
#undef UMCUB_CFG_UART_TX_PIN
#define UMCUB_CFG_UART_TX_PIN           UMCUB_PIN('B', 10, 0)
#undef UMCUB_CFG_UART_RX_PIN
#define UMCUB_CFG_UART_RX_PIN           UMCUB_PIN('B', 11, 0)
#undef UMCUB_CFG_UART_DE_PIN
#define UMCUB_CFG_UART_DE_PIN           UMCUB_PIN('B', 12, 0)
#undef UMCUB_CFG_UART_TURNAROUND_MS
#define UMCUB_CFG_UART_TURNAROUND_MS    1
#undef UMCUB_CFG_LOG_LEVEL
#define UMCUB_CFG_LOG_LEVEL             0
