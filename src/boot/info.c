/*
 * Bootloader info block, placed by the linker script at
 * UMCUB_CFG_BOOT_INFO_ADDR (end of the bootloader region).
 */
#include "umcub_cfg.h"
#include "umcub_handoff.h"
#include "umcub_version.h"

#define TBIT(id, en) ((en) ? (1u << (id)) : 0u)

__attribute__((section(".umcub_info"), used))
const umcub_info_block_t umcub_info_block = {
    .magic = UMCUB_INFO_MAGIC,
    .version = 1,
    .size = sizeof(umcub_info_block_t),
    .boot_version = { UMCUB_VERSION_MAJOR, UMCUB_VERSION_MINOR, UMCUB_VERSION_PATCH, 0 },
    .git_hash = UMCUB_GIT_HASH,
    .upgrade_mode = UMCUB_CFG_UPGRADE_MODE,
    .image_count = UMCUB_CFG_IMAGE_NUMBER,
    .dualcore_mode = UMCUB_CFG_DUALCORE_MODE,
    .transports = TBIT(UMCUB_TRANSPORT_UART, UMCUB_CFG_TRANSPORT_UART) |
                  TBIT(UMCUB_TRANSPORT_USB_CDC, UMCUB_CFG_TRANSPORT_USB_CDC) |
                  TBIT(UMCUB_TRANSPORT_USB_DFU, UMCUB_CFG_TRANSPORT_USB_DFU) |
                  TBIT(UMCUB_TRANSPORT_CAN, UMCUB_CFG_TRANSPORT_CAN) |
                  TBIT(UMCUB_TRANSPORT_ETH, UMCUB_CFG_TRANSPORT_ETH),
    .slot_addr = { { UMCUB_CFG_IMG0_PRIMARY_ADDR, UMCUB_CFG_IMG0_SECONDARY_ADDR },
                   { UMCUB_CFG_IMG1_PRIMARY_ADDR, UMCUB_CFG_IMG1_SECONDARY_ADDR } },
    .slot_size = { { UMCUB_CFG_IMG0_PRIMARY_SIZE, UMCUB_CFG_IMG0_SECONDARY_SIZE },
                   { UMCUB_CFG_IMG1_PRIMARY_SIZE, UMCUB_CFG_IMG1_SECONDARY_SIZE } },
    .header_size = UMCUB_CFG_IMAGE_HEADER_SIZE,
    .board = UMCUB_BOARD_NAME,
};
