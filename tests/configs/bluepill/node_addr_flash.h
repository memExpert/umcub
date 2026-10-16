/* Overlay (POST), Blue Pill: RS485 link with the node address read from flash
 * (UMCUB_CFG_NODE_ADDR_FLASH). The 64 KiB part has no page outside the
 * bootloader and the slots: the bench uses the spare page at the end of the
 * 32 KiB bootloader region (below the info block). A product puts the word
 * into the page that holds the application's settings. */
#include "rs485_link.h"
#undef UMCUB_CFG_NODE_ADDR_FLASH
#define UMCUB_CFG_NODE_ADDR_FLASH       0x08007C00
