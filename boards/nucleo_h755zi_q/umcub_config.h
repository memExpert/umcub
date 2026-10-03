/*
 * umcub configuration for NUCLEO-H755ZI-Q (STM32H755ZIT6, CM7 + CM4).
 *
 * Flash: 2 banks x 8 sectors x 128 KiB.
 *
 * UMCUB_DUALCORE_SINGLE_BOOT (default):
 *   bank1: S0 boot | S1-S3 img0 (CM7) primary | S4-S6 img0 secondary | S7 scratch
 *   bank2: S0-S2 img1 (CM4) primary | S3-S5 img1 secondary | S6-S7 free
 *   Option bytes: BCM7=1, BCM4=1, BOOT_CM7_ADD0=0x0800, BOOT_CM4_ADD0=0x0800
 *   (tools/h755_option_bytes.sh single)
 *
 * UMCUB_DUALCORE_PER_CORE:
 *   bank1: S0 CM7 boot | S1-S3 CM7 primary | S4-S6 CM7 secondary | S7 scratch
 *   bank2: S0 CM4 boot | S1-S3 CM4 primary | S4-S6 CM4 secondary | S7 scratch
 *   Option bytes: BOOT_CM4_ADD0=0x0810 (tools/h755_option_bytes.sh per-core)
 */
#ifndef UMCUB_CONFIG_H
#define UMCUB_CONFIG_H

#define UMCUB_CFG_MCU                   STM32H755xx

#ifndef UMCUB_CFG_DUALCORE_MODE
#define UMCUB_CFG_DUALCORE_MODE         UMCUB_DUALCORE_SINGLE_BOOT
#endif

/* --- clocks / power: 8 MHz HSE from ST-LINK MCO, SMPS direct ------------- */
#define UMCUB_CFG_CLOCK_SOURCE          UMCUB_CLK_HSE
#define UMCUB_CFG_HSE_HZ                8000000
#define UMCUB_CFG_HSE_BYPASS            1
#define UMCUB_CFG_PWR_SUPPLY            UMCUB_H7_SUPPLY_DIRECT_SMPS

/* --- MCUboot ------------------------------------------------------------- */
#ifndef UMCUB_CFG_UPGRADE_MODE
#define UMCUB_CFG_UPGRADE_MODE          UMCUB_MODE_SWAP_SCRATCH
#endif
#define UMCUB_CFG_IMAGE_HEADER_SIZE     0x400

/* --- flash layout -------------------------------------------------------- */
#define H755_SECTOR                     UMCUB_KB(128)
#define H755_BANK1                      0x08000000
#define H755_BANK2                      0x08100000

/* Sector arithmetic shared by both cores' slot sets: base = bank of the
 * images, first = first image sector. Primary gets one extra sector for
 * swap-move, secondary one extra for swap-offset. */
#if UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_SWAP_MOVE
#define H755_PRI_SECTORS 4
#define H755_SEC_SECTORS 3
#elif UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_SWAP_OFFSET
#define H755_PRI_SECTORS 3
#define H755_SEC_SECTORS 4
#else
#define H755_PRI_SECTORS 3
#define H755_SEC_SECTORS 3
#endif

#if UMCUB_CFG_DUALCORE_MODE == UMCUB_DUALCORE_PER_CORE && defined(UMCUB_CORE_CM4)
/* CM4 bootloader instance lives in bank 2 and manages a CM4 image there. */
#define UMCUB_CFG_BOOT_ADDR             H755_BANK2
#define UMCUB_CFG_BOOT_SIZE             H755_SECTOR
#define UMCUB_CFG_IMAGE_NUMBER          1
#define UMCUB_CFG_IMG0_PRIMARY_ADDR     (H755_BANK2 + 1 * H755_SECTOR)
#define UMCUB_CFG_IMG0_PRIMARY_SIZE     (H755_PRI_SECTORS * H755_SECTOR)
#define UMCUB_CFG_IMG0_SECONDARY_ADDR   (UMCUB_CFG_IMG0_PRIMARY_ADDR + UMCUB_CFG_IMG0_PRIMARY_SIZE)
#define UMCUB_CFG_IMG0_SECONDARY_SIZE   (H755_SEC_SECTORS * H755_SECTOR)
#if UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_SWAP_SCRATCH
#define UMCUB_CFG_SCRATCH_ADDR          (H755_BANK2 + 7 * H755_SECTOR)
#define UMCUB_CFG_SCRATCH_SIZE          H755_SECTOR
#endif
#define UMCUB_CFG_SHARED_RAM_ADDR       0x3800FE00
#else
/* Main (CM7) bootloader. */
#define UMCUB_CFG_BOOT_ADDR             H755_BANK1
#define UMCUB_CFG_BOOT_SIZE             H755_SECTOR
#define UMCUB_CFG_IMG0_PRIMARY_ADDR     (H755_BANK1 + 1 * H755_SECTOR)
#define UMCUB_CFG_IMG0_PRIMARY_SIZE     (H755_PRI_SECTORS * H755_SECTOR)
#define UMCUB_CFG_IMG0_SECONDARY_ADDR   (UMCUB_CFG_IMG0_PRIMARY_ADDR + UMCUB_CFG_IMG0_PRIMARY_SIZE)
#define UMCUB_CFG_IMG0_SECONDARY_SIZE   (H755_SEC_SECTORS * H755_SECTOR)
#if UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_SWAP_SCRATCH
#define UMCUB_CFG_SCRATCH_ADDR          (H755_BANK1 + 7 * H755_SECTOR)
#define UMCUB_CFG_SCRATCH_SIZE          H755_SECTOR
#endif
#if UMCUB_CFG_DUALCORE_MODE == UMCUB_DUALCORE_SINGLE_BOOT
/* Image 1 = CM4 application in bank 2. */
#define UMCUB_CFG_IMAGE_NUMBER          2
#define UMCUB_CFG_IMG1_PRIMARY_ADDR     H755_BANK2
#define UMCUB_CFG_IMG1_PRIMARY_SIZE     (H755_PRI_SECTORS * H755_SECTOR)
#define UMCUB_CFG_IMG1_SECONDARY_ADDR   (UMCUB_CFG_IMG1_PRIMARY_ADDR + UMCUB_CFG_IMG1_PRIMARY_SIZE)
#define UMCUB_CFG_IMG1_SECONDARY_SIZE   (H755_SEC_SECTORS * H755_SECTOR)
#endif
#define UMCUB_CFG_SHARED_RAM_ADDR       0x3800FF00
#endif

/* --- entry --------------------------------------------------------------- */
#define UMCUB_CFG_ENTRY_GPIO            1
#define UMCUB_CFG_ENTRY_GPIO_PIN        UMCUB_PIN('C', 13, 0)   /* B1 USER */
#define UMCUB_CFG_ENTRY_GPIO_ACTIVE     1
#define UMCUB_CFG_ENTRY_WAIT_MS         0

/* --- text commands (UART / USB CDC typed text, non-SMP UDP/CAN packets) -- */
#define UMCUB_CFG_CMD_ENABLE            1
#define UMCUB_CFG_CMD_IMMEDIATE         1       /* single key, no Enter needed */
#define UMCUB_CFG_CMD_TABLE             UMCUB_CMD("a",     UMCUB_CMD_BOOT_APP)  \
                                        UMCUB_CMD("b",     UMCUB_CMD_STAY)      \
                                        UMCUB_CMD("r",     UMCUB_CMD_RESET)     \
                                        UMCUB_CMD("i",     UMCUB_CMD_INFO)      \
                                        UMCUB_CMD("hello", UMCUB_CMD_USER(1))   /* umcub_board.c */ \
                                        UMCUB_CMD_INSPECT_DEFAULTS              /* verify, hash (+ read) */

/* --- transports ---------------------------------------------------------- */
#if defined(UMCUB_CORE_CM4)
/* PER_CORE: the CM4 instance uses CAN only (peripherals must not overlap). */
#define UMCUB_CFG_TRANSPORT_UART        0
#define UMCUB_CFG_TRANSPORT_CAN         1
#define UMCUB_CFG_LOG_LEVEL             0
#else
#define UMCUB_CFG_TRANSPORT_UART        1
#define UMCUB_CFG_UART_INSTANCE         3                       /* ST-LINK VCP */
#define UMCUB_CFG_UART_BAUD             115200
#define UMCUB_CFG_UART_TX_PIN           UMCUB_PIN('D', 8, 7)
#define UMCUB_CFG_UART_RX_PIN           UMCUB_PIN('D', 9, 7)

#define UMCUB_CFG_TRANSPORT_USB_CDC     1                       /* CN13 USB user */
#define UMCUB_CFG_TRANSPORT_USB_DFU     1

#define UMCUB_CFG_TRANSPORT_ETH         1                       /* LAN8742A */
#define UMCUB_CFG_ETH_PHY_ADDR          0
#define UMCUB_CFG_ETH_RMII_PINS         UMCUB_PIN('A', 1, 11),  /* REF_CLK */ \
                                        UMCUB_PIN('A', 2, 11),  /* MDIO    */ \
                                        UMCUB_PIN('A', 7, 11),  /* CRS_DV  */ \
                                        UMCUB_PIN('C', 1, 11),  /* MDC     */ \
                                        UMCUB_PIN('C', 4, 11),  /* RXD0    */ \
                                        UMCUB_PIN('C', 5, 11),  /* RXD1    */ \
                                        UMCUB_PIN('G', 11, 11), /* TX_EN   */ \
                                        UMCUB_PIN('G', 13, 11), /* TXD0    */ \
                                        UMCUB_PIN('B', 13, 11)  /* TXD1    */

#if UMCUB_CFG_DUALCORE_MODE == UMCUB_DUALCORE_PER_CORE
#define UMCUB_CFG_TRANSPORT_CAN         0                       /* owned by CM4 */
#else
#define UMCUB_CFG_TRANSPORT_CAN         1                       /* no transceiver on board */
#endif
#endif

#define UMCUB_CFG_CAN_INSTANCE          1
#define UMCUB_CFG_CAN_TX_PIN            UMCUB_PIN('D', 1, 9)
#define UMCUB_CFG_CAN_RX_PIN            UMCUB_PIN('D', 0, 9)

#endif /* UMCUB_CONFIG_H */
