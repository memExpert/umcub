/* UMCUB_CONFIG_POST for test_link_secure.c: umcub link SECURE with payload
 * encryption; RDP comes from fake_rdp_level. */
#include "test_link_post.h"
#undef UMCUB_CFG_UART_LINK
#define UMCUB_CFG_UART_LINK UMCUB_LINK_SECURE
#undef UMCUB_CFG_LINK_ENCRYPT
#define UMCUB_CFG_LINK_ENCRYPT 1
#undef UMCUB_CFG_TRANSPORT_USB_DFU
#define UMCUB_CFG_TRANSPORT_USB_DFU 0
/* Encrypted images: readback only inside the encrypted session. */
#undef UMCUB_CFG_ENCRYPT_IMAGES
#define UMCUB_CFG_ENCRYPT_IMAGES 1
#undef UMCUB_CFG_READBACK
#define UMCUB_CFG_READBACK 1
#undef UMCUB_CFG_CMD_TABLE
#define UMCUB_CFG_CMD_TABLE UMCUB_CMD("i", UMCUB_CMD_INFO) UMCUB_CMD("read", UMCUB_CMD_READ)
