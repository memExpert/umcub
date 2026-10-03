/* Overlay (POST), Blue Pill: RS485 with umcub link SECURE (host authentication
 * and session MAC, no payload encryption). */
#include "bluepill_rs485.h"
#undef UMCUB_CFG_UART_LINK
#define UMCUB_CFG_UART_LINK             UMCUB_LINK_SECURE
