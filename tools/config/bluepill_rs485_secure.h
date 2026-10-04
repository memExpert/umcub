/* Overlay (POST), Blue Pill: RS485 with umcub link SECURE (host authentication
 * and session MAC, no payload encryption). The ECC code does not leave room in
 * the 32 KiB default region: bootloader 36 KiB + two 14 KiB slots. */
#include "bluepill_rs485.h"
#undef UMCUB_CFG_UART_LINK
#define UMCUB_CFG_UART_LINK             UMCUB_LINK_SECURE

#undef UMCUB_CFG_BOOT_SIZE
#undef UMCUB_CFG_IMG0_PRIMARY_ADDR
#undef UMCUB_CFG_IMG0_PRIMARY_SIZE
#undef UMCUB_CFG_IMG0_SECONDARY_ADDR
#undef UMCUB_CFG_IMG0_SECONDARY_SIZE
#define UMCUB_CFG_BOOT_SIZE             UMCUB_KB(36)
#define UMCUB_CFG_IMG0_PRIMARY_ADDR     0x08009000
#define UMCUB_CFG_IMG0_PRIMARY_SIZE     UMCUB_KB(14)
#define UMCUB_CFG_IMG0_SECONDARY_ADDR   0x0800C800
#define UMCUB_CFG_IMG0_SECONDARY_SIZE   UMCUB_KB(14)
