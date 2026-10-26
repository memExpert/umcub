/* Overlay (POST), Black Pill F411: RS485 on USART1 (DE on PA8) with umcub link
 * addressing - the shared-bus setup (needs the ADC entropy source). */
#include "rs485.h"
#undef UMCUB_CFG_UART_LINK
#define UMCUB_CFG_UART_LINK             UMCUB_LINK_ADDRESSED
