/* UMCUB_CONFIG_POST: UART transmit through a 512-byte RAM ring drained by the
 * interrupt (UMCUB_CFG_UART_TX_RING): the boot log does not stall the boot. */
#undef UMCUB_CFG_UART_TX_RING
#define UMCUB_CFG_UART_TX_RING          512
