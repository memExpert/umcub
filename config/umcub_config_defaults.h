/*
 * Default values for every UMCUB_CFG_* option not set by the board's
 * umcub_config.h. Family specific defaults (umcub_family_defaults.h from the
 * port) are applied before these. Documentation: umcub_config_template.h.
 */
#ifndef UMCUB_CONFIG_DEFAULTS_H
#define UMCUB_CONFIG_DEFAULTS_H

/* --- target -------------------------------------------------------------- */
#ifndef UMCUB_CFG_DUALCORE_MODE
#define UMCUB_CFG_DUALCORE_MODE         UMCUB_DUALCORE_NONE
#endif

/* --- clocks -------------------------------------------------------------- */
#ifndef UMCUB_CFG_CLOCK_SOURCE
#define UMCUB_CFG_CLOCK_SOURCE          UMCUB_CLK_HSI
#endif
#ifndef UMCUB_CFG_HSE_HZ
#define UMCUB_CFG_HSE_HZ                8000000
#endif
#ifndef UMCUB_CFG_HSE_BYPASS
#define UMCUB_CFG_HSE_BYPASS            0
#endif

/* --- layout -------------------------------------------------------------- */
#ifndef UMCUB_CFG_IMAGE_NUMBER
#if UMCUB_CFG_DUALCORE_MODE == UMCUB_DUALCORE_SINGLE_BOOT
#define UMCUB_CFG_IMAGE_NUMBER          2
#else
#define UMCUB_CFG_IMAGE_NUMBER          1
#endif
#endif
#ifndef UMCUB_CFG_IMAGE_HEADER_SIZE
#define UMCUB_CFG_IMAGE_HEADER_SIZE     0x400
#endif
#ifndef UMCUB_CFG_SCRATCH_ADDR
#define UMCUB_CFG_SCRATCH_ADDR          0
#define UMCUB_CFG_SCRATCH_SIZE          0
#endif
#ifndef UMCUB_CFG_IMG1_PRIMARY_ADDR
#define UMCUB_CFG_IMG1_PRIMARY_ADDR     0
#define UMCUB_CFG_IMG1_PRIMARY_SIZE     0
#define UMCUB_CFG_IMG1_SECONDARY_ADDR   0
#define UMCUB_CFG_IMG1_SECONDARY_SIZE   0
#endif
/* Bootloader info block: last 256 bytes of the bootloader region. */
#ifndef UMCUB_CFG_BOOT_INFO_ADDR
#define UMCUB_CFG_BOOT_INFO_ADDR        (UMCUB_CFG_BOOT_ADDR + UMCUB_CFG_BOOT_SIZE - 256)
#endif

/* --- MCUboot ------------------------------------------------------------- */
#ifndef UMCUB_CFG_UPGRADE_MODE
#define UMCUB_CFG_UPGRADE_MODE          UMCUB_MODE_OVERWRITE
#endif
#ifndef UMCUB_CFG_SIGNATURE
#define UMCUB_CFG_SIGNATURE             UMCUB_SIGN_EC256
#endif
#ifndef UMCUB_CFG_DOWNGRADE_PREVENTION
#define UMCUB_CFG_DOWNGRADE_PREVENTION  0
#endif
#ifndef UMCUB_CFG_VALIDATE_PRIMARY
#define UMCUB_CFG_VALIDATE_PRIMARY      1
#endif

/* --- entry --------------------------------------------------------------- */
#ifndef UMCUB_CFG_ENTRY_ON_REQUEST
#define UMCUB_CFG_ENTRY_ON_REQUEST      1
#endif
#ifndef UMCUB_CFG_ENTRY_ON_NO_IMAGE
#define UMCUB_CFG_ENTRY_ON_NO_IMAGE     1
#endif
#ifndef UMCUB_CFG_ENTRY_GPIO
#define UMCUB_CFG_ENTRY_GPIO            0
#endif
#ifndef UMCUB_CFG_ENTRY_GPIO_PIN
#define UMCUB_CFG_ENTRY_GPIO_PIN        UMCUB_PIN('A', 0, 0)
#endif
#ifndef UMCUB_CFG_ENTRY_GPIO_ACTIVE
#define UMCUB_CFG_ENTRY_GPIO_ACTIVE     1
#endif
#ifndef UMCUB_CFG_ENTRY_GPIO_PULL
#define UMCUB_CFG_ENTRY_GPIO_PULL       UMCUB_PULL_NONE
#endif
#ifndef UMCUB_CFG_ENTRY_WAIT_MS
#define UMCUB_CFG_ENTRY_WAIT_MS         0
#endif
#ifndef UMCUB_CFG_RECOVERY_TIMEOUT_MS
#define UMCUB_CFG_RECOVERY_TIMEOUT_MS   0
#endif

/* --- text commands ------------------------------------------------------- */
#ifndef UMCUB_CFG_CMD_ENABLE
#define UMCUB_CFG_CMD_ENABLE            0
#endif
#ifndef UMCUB_CFG_CMD_IMMEDIATE
#define UMCUB_CFG_CMD_IMMEDIATE         1
#endif
#ifndef UMCUB_CFG_CMD_REPLY
#define UMCUB_CFG_CMD_REPLY             1
#endif
/* --- slot inspection for hosts (text commands + SMP group) ---------------- */
#ifndef UMCUB_CFG_INSPECT_VERIFY
#define UMCUB_CFG_INSPECT_VERIFY        1
#endif
#ifndef UMCUB_CFG_INSPECT_HASH
#define UMCUB_CFG_INSPECT_HASH          1
#endif
#ifndef UMCUB_CFG_READBACK
#define UMCUB_CFG_READBACK              0
#endif
#ifndef UMCUB_CFG_SMP_INSPECT_GROUP
#define UMCUB_CFG_SMP_INSPECT_GROUP     100
#endif
#define UMCUB_CFG_INSPECT (UMCUB_CFG_INSPECT_VERIFY | UMCUB_CFG_INSPECT_HASH | UMCUB_CFG_READBACK)

#if UMCUB_CFG_INSPECT_VERIFY
#define UMCUB_DEFAULT_CMD_VERIFY        UMCUB_CMD("verify", UMCUB_CMD_VERIFY)
#else
#define UMCUB_DEFAULT_CMD_VERIFY
#endif
#if UMCUB_CFG_INSPECT_HASH
#define UMCUB_DEFAULT_CMD_HASH          UMCUB_CMD("hash", UMCUB_CMD_HASH)
#else
#define UMCUB_DEFAULT_CMD_HASH
#endif
#if UMCUB_CFG_READBACK
#define UMCUB_DEFAULT_CMD_READ          UMCUB_CMD("read", UMCUB_CMD_READ)
#else
#define UMCUB_DEFAULT_CMD_READ
#endif
/* Use UMCUB_CMD_INSPECT_DEFAULTS in your own table to get the three above. */
#define UMCUB_CMD_INSPECT_DEFAULTS      UMCUB_DEFAULT_CMD_VERIFY UMCUB_DEFAULT_CMD_HASH UMCUB_DEFAULT_CMD_READ

#ifndef UMCUB_CFG_CMD_TABLE
#define UMCUB_CFG_CMD_TABLE             UMCUB_CMD("a", UMCUB_CMD_BOOT_APP) \
                                        UMCUB_CMD("b", UMCUB_CMD_STAY)     \
                                        UMCUB_CMD("r", UMCUB_CMD_RESET)    \
                                        UMCUB_CMD("i", UMCUB_CMD_INFO)     \
                                        UMCUB_CMD_INSPECT_DEFAULTS
#endif

/* --- UART ---------------------------------------------------------------- */
#ifndef UMCUB_CFG_TRANSPORT_UART
#define UMCUB_CFG_TRANSPORT_UART        0
#endif
#ifndef UMCUB_CFG_UART_INSTANCE
#define UMCUB_CFG_UART_INSTANCE         1
#endif
#ifndef UMCUB_CFG_UART_BAUD
#define UMCUB_CFG_UART_BAUD             115200
#endif

/* --- USB ----------------------------------------------------------------- */
#ifndef UMCUB_CFG_TRANSPORT_USB_CDC
#define UMCUB_CFG_TRANSPORT_USB_CDC     0
#endif
#ifndef UMCUB_CFG_TRANSPORT_USB_DFU
#define UMCUB_CFG_TRANSPORT_USB_DFU     0
#endif
#define UMCUB_CFG_USB                   (UMCUB_CFG_TRANSPORT_USB_CDC | UMCUB_CFG_TRANSPORT_USB_DFU)
#ifndef UMCUB_CFG_USB_VID
#define UMCUB_CFG_USB_VID               0x1209
#endif
#ifndef UMCUB_CFG_USB_PID
#define UMCUB_CFG_USB_PID               0x0001
#endif
#ifndef UMCUB_CFG_USB_MANUFACTURER
#define UMCUB_CFG_USB_MANUFACTURER      "umcub"
#endif
#ifndef UMCUB_CFG_USB_PRODUCT
#define UMCUB_CFG_USB_PRODUCT           "umcub bootloader"
#endif
#ifndef UMCUB_CFG_USB_DFU_IMAGE
#define UMCUB_CFG_USB_DFU_IMAGE         0
#endif

/* --- CAN ----------------------------------------------------------------- */
#ifndef UMCUB_CFG_TRANSPORT_CAN
#define UMCUB_CFG_TRANSPORT_CAN         0
#endif
#ifndef UMCUB_CFG_CAN_INSTANCE
#define UMCUB_CFG_CAN_INSTANCE          1
#endif
#ifndef UMCUB_CFG_CAN_BITRATE
#define UMCUB_CFG_CAN_BITRATE           500000
#endif
#ifndef UMCUB_CFG_CAN_FD
#define UMCUB_CFG_CAN_FD                0
#endif
#ifndef UMCUB_CFG_CAN_DATA_BITRATE
#define UMCUB_CFG_CAN_DATA_BITRATE      2000000
#endif
#ifndef UMCUB_CFG_CAN_RX_ID
#define UMCUB_CFG_CAN_RX_ID             0x7C0
#endif
#ifndef UMCUB_CFG_CAN_TX_ID
#define UMCUB_CFG_CAN_TX_ID             0x7C8
#endif
#ifndef UMCUB_CFG_CAN_EXT_ID
#define UMCUB_CFG_CAN_EXT_ID            0
#endif
#ifndef UMCUB_CFG_CAN_LOOPBACK
#define UMCUB_CFG_CAN_LOOPBACK          0
#endif

/* --- Ethernet ------------------------------------------------------------ */
#ifndef UMCUB_CFG_TRANSPORT_ETH
#define UMCUB_CFG_TRANSPORT_ETH         0
#endif
/* Derived: some transport speaks SMP (boot_serial, zcbor, SMP inspection
 * group are compiled only then; USB DFU alone does not need them). */
#define UMCUB_CFG_SMP                   (UMCUB_CFG_TRANSPORT_UART | UMCUB_CFG_TRANSPORT_USB_CDC | \
                                         UMCUB_CFG_TRANSPORT_CAN | UMCUB_CFG_TRANSPORT_ETH)
#ifndef UMCUB_CFG_ETH_DHCP
#define UMCUB_CFG_ETH_DHCP              1
#endif
#ifndef UMCUB_CFG_ETH_IP
#define UMCUB_CFG_ETH_IP                UMCUB_IP4(192, 168, 1, 50)
#endif
#ifndef UMCUB_CFG_ETH_NETMASK
#define UMCUB_CFG_ETH_NETMASK           UMCUB_IP4(255, 255, 255, 0)
#endif
#ifndef UMCUB_CFG_ETH_GATEWAY
#define UMCUB_CFG_ETH_GATEWAY           UMCUB_IP4(192, 168, 1, 1)
#endif
#ifndef UMCUB_CFG_ETH_DHCP_TIMEOUT_MS
#define UMCUB_CFG_ETH_DHCP_TIMEOUT_MS   10000
#endif
#ifndef UMCUB_CFG_ETH_MAC
#define UMCUB_CFG_ETH_MAC               0
#endif
#ifndef UMCUB_CFG_ETH_SMP_PORT
#define UMCUB_CFG_ETH_SMP_PORT          1337
#endif
#ifndef UMCUB_CFG_ETH_PHY_ADDR
#define UMCUB_CFG_ETH_PHY_ADDR          0
#endif

/* --- misc ---------------------------------------------------------------- */
#ifndef UMCUB_CFG_LOG_LEVEL
#define UMCUB_CFG_LOG_LEVEL             3
#endif
#ifndef UMCUB_CFG_WATCHDOG_MS
#define UMCUB_CFG_WATCHDOG_MS           0
#endif
#ifndef UMCUB_CFG_APP_WRITE_SLOT
#if UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_DIRECT_XIP || UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_DIRECT_XIP_REVERT
#define UMCUB_CFG_APP_WRITE_SLOT        UMCUB_SLOT_INACTIVE
#else
#define UMCUB_CFG_APP_WRITE_SLOT        UMCUB_SLOT_SECONDARY
#endif
#endif

/* TEST ONLY: reset in the middle of the K-th flash operation (power-loss
 * emulation, tools/hw/powerfail_test.py). Never enable in production. */
#ifndef UMCUB_CFG_TEST_FAULT_INJECT
#define UMCUB_CFG_TEST_FAULT_INJECT     0
#endif
#ifndef UMCUB_CFG_TEST_FAULT_ADDR
#define UMCUB_CFG_TEST_FAULT_ADDR       0x3800F000
#endif

/* SMP upload "image" field addresses slots directly (see mcuboot_config.h). */
#ifndef UMCUB_CFG_SMP_DIRECT_UPLOAD
#define UMCUB_CFG_SMP_DIRECT_UPLOAD     0
#endif

/* SMP packet size limit (decoded). Large enough for one MTU of any transport. */
#ifndef UMCUB_CFG_SMP_MTU
#define UMCUB_CFG_SMP_MTU               1024
#endif

#endif /* UMCUB_CONFIG_DEFAULTS_H */
