/* Overlay (POST), Blue Pill: RS485 on USART1 (software DE on PB12) with umcub
 * link addressing - the shared-bus setup. */
#include "bluepill_rs485.h"
#undef UMCUB_CFG_UART_LINK
#define UMCUB_CFG_UART_LINK             UMCUB_LINK_ADDRESSED
