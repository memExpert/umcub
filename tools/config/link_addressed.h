/* Overlay (POST): umcub link addressing on every transport (several devices
 * on one bus); the host side is tools/umcub_link.py. */
#undef UMCUB_CFG_UART_LINK
#undef UMCUB_CFG_USB_CDC_LINK
#undef UMCUB_CFG_CAN_LINK
#undef UMCUB_CFG_ETH_LINK
#define UMCUB_CFG_UART_LINK             UMCUB_LINK_ADDRESSED
#define UMCUB_CFG_USB_CDC_LINK          UMCUB_LINK_ADDRESSED
#define UMCUB_CFG_CAN_LINK              UMCUB_LINK_ADDRESSED
#define UMCUB_CFG_ETH_LINK              UMCUB_LINK_ADDRESSED
