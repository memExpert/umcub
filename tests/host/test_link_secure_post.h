/* UMCUB_CONFIG_POST for test_link_secure.c: umcub link SECURE with payload
 * encryption; RDP comes from fake_rdp_level. */
#include "test_link_post.h"
#undef UMCUB_CFG_UART_LINK
#define UMCUB_CFG_UART_LINK UMCUB_LINK_SECURE
#undef UMCUB_CFG_LINK_ENCRYPT
#define UMCUB_CFG_LINK_ENCRYPT 1
#undef UMCUB_CFG_TRANSPORT_USB_DFU
#define UMCUB_CFG_TRANSPORT_USB_DFU 0
