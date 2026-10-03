/* Overlay (POST): umcub link SECURE on every transport with payload encryption
 * (host authentication, session MAC, AES-CTR). USB DFU cannot authenticate the
 * host and is left out. */
#include "link_addressed.h"
#undef UMCUB_CFG_UART_LINK
#undef UMCUB_CFG_USB_CDC_LINK
#undef UMCUB_CFG_CAN_LINK
#undef UMCUB_CFG_ETH_LINK
#define UMCUB_CFG_UART_LINK             UMCUB_LINK_SECURE
#define UMCUB_CFG_USB_CDC_LINK          UMCUB_LINK_SECURE
#define UMCUB_CFG_CAN_LINK              UMCUB_LINK_SECURE
#define UMCUB_CFG_ETH_LINK              UMCUB_LINK_SECURE
#undef UMCUB_CFG_LINK_ENCRYPT
#define UMCUB_CFG_LINK_ENCRYPT          1
#undef UMCUB_CFG_TRANSPORT_USB_DFU
#define UMCUB_CFG_TRANSPORT_USB_DFU     0
