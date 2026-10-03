/* UMCUB_CONFIG_POST: UART transport on the Arduino D0/D1 pins of NUCLEO-H745/H755ZI-Q
 * (UM2408 Table 16/CN10: D1 = PB6 pin 14, D0 = PB7 pin 16, SB11/SB96 ON by default)
 * -> USART1 AF7 (DS12919 Table "Port B alternate functions"). */
#undef UMCUB_CFG_UART_INSTANCE
#undef UMCUB_CFG_UART_TX_PIN
#undef UMCUB_CFG_UART_RX_PIN
#define UMCUB_CFG_UART_INSTANCE 1
#define UMCUB_CFG_UART_TX_PIN   UMCUB_PIN('B', 6, 7)
#define UMCUB_CFG_UART_RX_PIN   UMCUB_PIN('B', 7, 7)
