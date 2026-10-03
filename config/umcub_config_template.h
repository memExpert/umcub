/*
 * umcub configuration template.
 *
 * Copy this file to boards/<your_board>/umcub_config.h and uncomment / change
 * only what you need. Everything left commented out takes the default from
 * config/umcub_config_defaults.h. This one header is the single source of
 * truth: CMake reads it to decide what to compile, the C code includes it and
 * the linker scripts (bootloader and application) are preprocessed with it.
 *
 * Build-time context available here:
 *   UMCUB_CORE_CM7 / UMCUB_CORE_CM4  - defined to 1 for the core being built
 *                                      (dual-core parts, -DUMCUB_CORE=cm7|cm4)
 *   UMCUB_BUILDING_APP               - defined when an application includes
 *                                      this file through umcub::app
 *
 * Helper macros (from umcub_config_types.h): UMCUB_KB(n), UMCUB_MB(n),
 * UMCUB_PIN(port, pin, af), UMCUB_IP4(a, b, c, d).
 */
#ifndef UMCUB_CONFIG_H
#define UMCUB_CONFIG_H

/* ======================================================================== */
/* Target                                                                   */
/* ======================================================================== */

/* CMSIS device define of the MCU (selects family port, startup, CPU flags). */
#define UMCUB_CFG_MCU                     STM32H755xx

/* Dual-core handling (only for dual-core parts such as STM32H745/H755/H747):
 *   UMCUB_DUALCORE_NONE        - single core part or second core unused
 *   UMCUB_DUALCORE_SINGLE_BOOT - one bootloader on the main core manages two
 *                                images (image 0 = main core, image 1 = second
 *                                core) and releases the second core
 *   UMCUB_DUALCORE_PER_CORE    - every core runs its own bootloader instance
 *                                with its own image(s) and transports        */
/* #define UMCUB_CFG_DUALCORE_MODE        UMCUB_DUALCORE_NONE */

/* ======================================================================== */
/* Clocks / power (family port computes the PLL settings)                  */
/* ======================================================================== */

/* UMCUB_CLK_HSI or UMCUB_CLK_HSE */
/* #define UMCUB_CFG_CLOCK_SOURCE         UMCUB_CLK_HSI */
/* #define UMCUB_CFG_HSE_HZ               8000000 */
/* #define UMCUB_CFG_HSE_BYPASS           0 */
/* Family specific power supply; for STM32H7: UMCUB_H7_SUPPLY_LDO,
 * UMCUB_H7_SUPPLY_DIRECT_SMPS, UMCUB_H7_SUPPLY_SMPS_1V8_LDO, ...
 * MUST match the hardware and the application (write-once register).      */
/* #define UMCUB_CFG_PWR_SUPPLY           UMCUB_H7_SUPPLY_LDO */

/* ======================================================================== */
/* Flash layout                                                             */
/* ======================================================================== */

/* Bootloader region (must cover whole erase sectors). */
/* #define UMCUB_CFG_BOOT_ADDR            0x08000000 */
/* #define UMCUB_CFG_BOOT_SIZE            UMCUB_KB(128) */

/* Number of MCUboot images (1 or 2; 2 for UMCUB_DUALCORE_SINGLE_BOOT). */
/* #define UMCUB_CFG_IMAGE_NUMBER         1 */

/* Image slots: address and size of primary/secondary slot per image. */
/* #define UMCUB_CFG_IMG0_PRIMARY_ADDR    0x08020000 */
/* #define UMCUB_CFG_IMG0_PRIMARY_SIZE    UMCUB_KB(384) */
/* #define UMCUB_CFG_IMG0_SECONDARY_ADDR  0x08080000 */
/* #define UMCUB_CFG_IMG0_SECONDARY_SIZE  UMCUB_KB(384) */
/* #define UMCUB_CFG_IMG1_PRIMARY_ADDR    ... */
/* #define UMCUB_CFG_IMG1_PRIMARY_SIZE    ... */
/* #define UMCUB_CFG_IMG1_SECONDARY_ADDR  ... */
/* #define UMCUB_CFG_IMG1_SECONDARY_SIZE  ... */

/* Scratch area (only UMCUB_MODE_SWAP_SCRATCH). */
/* #define UMCUB_CFG_SCRATCH_ADDR         0x080E0000 */
/* #define UMCUB_CFG_SCRATCH_SIZE         UMCUB_KB(128) */

/* Image header size reserved by imgtool (--header-size). Must keep the
 * application vector table aligned as the core requires (VTOR). */
/* #define UMCUB_CFG_IMAGE_HEADER_SIZE    0x400 */

/* ======================================================================== */
/* MCUboot                                                                  */
/* ======================================================================== */

/* UMCUB_MODE_OVERWRITE, UMCUB_MODE_SWAP_SCRATCH, UMCUB_MODE_SWAP_MOVE,
 * UMCUB_MODE_SWAP_OFFSET, UMCUB_MODE_DIRECT_XIP, UMCUB_MODE_DIRECT_XIP_REVERT */
/* #define UMCUB_CFG_UPGRADE_MODE         UMCUB_MODE_OVERWRITE */

/* Signature: UMCUB_SIGN_EC256 (tinycrypt) or UMCUB_SIGN_NONE (hash only). */
/* #define UMCUB_CFG_SIGNATURE            UMCUB_SIGN_EC256 */

/* Refuse images with a lower version than the running one. */
/* #define UMCUB_CFG_DOWNGRADE_PREVENTION 0 */

/* Validate the primary slot signature on every boot (recommended). */
/* #define UMCUB_CFG_VALIDATE_PRIMARY     1 */

/* ======================================================================== */
/* Entering the bootloader (recovery / update mode)                         */
/* ======================================================================== */

/* Application asked for it via umcub_enter_bootloader(). */
/* #define UMCUB_CFG_ENTRY_ON_REQUEST     1 */
/* No bootable image found. */
/* #define UMCUB_CFG_ENTRY_ON_NO_IMAGE    1 */
/* Button / strap pin held at reset. */
/* #define UMCUB_CFG_ENTRY_GPIO           0 */
/* #define UMCUB_CFG_ENTRY_GPIO_PIN       UMCUB_PIN('C', 13, 0) */
/* #define UMCUB_CFG_ENTRY_GPIO_ACTIVE    1 */
/* #define UMCUB_CFG_ENTRY_GPIO_PULL      UMCUB_PULL_NONE */
/* Listen for an SMP request this long on every boot (0 = don't wait). */
/* #define UMCUB_CFG_ENTRY_WAIT_MS        0 */
/* Leave recovery mode (reset) after this much inactivity (0 = never). */
/* #define UMCUB_CFG_RECOVERY_TIMEOUT_MS  0 */

/* ======================================================================== */
/* Text commands                                                            */
/* ======================================================================== */

/* Simple text commands next to SMP, for a terminal or a few lines of host
 * code: on stream transports (UART, USB CDC) typed text, on packet transports
 * (UDP, CAN ISO-TP) a packet that is not an SMP frame. They are listened to
 * in recovery mode and during the UMCUB_CFG_ENTRY_WAIT_MS window, e.g.
 * "send b within 300 ms after reset to stay in the bootloader". */
/* #define UMCUB_CFG_CMD_ENABLE           0 */
/* 1: run a command as soon as the input equals it (single key "a");
 * 0: only after Enter (CR or LF). A command that is the beginning of
 * another one ("r" and "read") always waits for Enter. */
/* #define UMCUB_CFG_CMD_IMMEDIATE        1 */
/* Send "ok <cmd>" / "? <text>" replies. */
/* #define UMCUB_CFG_CMD_REPLY            1 */
/* Table of UMCUB_CMD("text", action). Actions: UMCUB_CMD_BOOT_APP,
 * UMCUB_CMD_STAY, UMCUB_CMD_RESET, UMCUB_CMD_INFO, UMCUB_CMD_USER(n).
 * UMCUB_CMD_USER(n) calls umcub_cmd_user(n, text, reply) - implement it in
 * boards/<board>/umcub_board.c (compiled automatically when present). */
/* #define UMCUB_CFG_CMD_TABLE            UMCUB_CMD("a", UMCUB_CMD_BOOT_APP) \
                                          UMCUB_CMD("b", UMCUB_CMD_STAY)     \
                                          UMCUB_CMD("r", UMCUB_CMD_RESET)    \
                                          UMCUB_CMD("i", UMCUB_CMD_INFO) */

/* ======================================================================== */
/* Slot inspection (host checks what was written)                           */
/* ======================================================================== */

/* "verify <image> <slot>": full MCUboot validation of the slot (SHA-256
 * over flash + signature) -> "ok valid" / "bad". */
/* #define UMCUB_CFG_INSPECT_VERIFY       1 */
/* "hash <image> <slot> [<off> <len>]": SHA-256 of a slot range (default:
 * the whole stored image = the .signed.bin that was sent). */
/* #define UMCUB_CFG_INSPECT_HASH         1 */
/* "read <image> <slot> <off> <len>", SMP read and USB DFU upload: raw
 * readback of the image slots. Exposes the firmware to anybody who can put
 * the device into recovery mode - off by default. The bootloader region is
 * never readable. */
/* #define UMCUB_CFG_READBACK             0 */
/* SMP group id of verify (0) / hash (1) / read (2) for host software. */
/* #define UMCUB_CFG_SMP_INSPECT_GROUP    100 */

/* ======================================================================== */
/* Transports                                                               */
/* ======================================================================== */

/* --- UART: SMP serial (mcumgr/smpmgr --conntype serial) ----------------- */
/* #define UMCUB_CFG_TRANSPORT_UART       1 */
/* #define UMCUB_CFG_UART_INSTANCE        3            (USART3) */
/* #define UMCUB_CFG_UART_BAUD            115200 */
/* #define UMCUB_CFG_UART_TX_PIN          UMCUB_PIN('D', 8, 7) */
/* #define UMCUB_CFG_UART_RX_PIN          UMCUB_PIN('D', 9, 7) */

/* --- USB device (tinyUSB): CDC-ACM with SMP and/or DFU ------------------ */
/* #define UMCUB_CFG_TRANSPORT_USB_CDC    0 */
/* #define UMCUB_CFG_TRANSPORT_USB_DFU    0 */
/* #define UMCUB_CFG_USB_VID              0x1209 */
/* #define UMCUB_CFG_USB_PID              0x0001 */
/* #define UMCUB_CFG_USB_MANUFACTURER     "umcub" */
/* #define UMCUB_CFG_USB_PRODUCT          "umcub bootloader" */
/* DFU download goes to this image's secondary slot (or the inactive slot in
 * direct-xip); after manifestation the image is marked pending.           */
/* #define UMCUB_CFG_USB_DFU_IMAGE        0 */

/* --- CAN / FDCAN: SMP over ISO-TP-lite ---------------------------------- */
/* #define UMCUB_CFG_TRANSPORT_CAN        0 */
/* #define UMCUB_CFG_CAN_INSTANCE         1 */
/* #define UMCUB_CFG_CAN_BITRATE          500000 */
/* #define UMCUB_CFG_CAN_FD               0     (1 = CAN-FD frames, 64 byte) */
/* #define UMCUB_CFG_CAN_DATA_BITRATE     2000000 */
/* #define UMCUB_CFG_CAN_RX_ID            0x7C0 (host -> device) */
/* #define UMCUB_CFG_CAN_TX_ID            0x7C8 (device -> host) */
/* #define UMCUB_CFG_CAN_EXT_ID           0 */
/* #define UMCUB_CFG_CAN_TX_PIN           UMCUB_PIN('D', 1, 9) */
/* #define UMCUB_CFG_CAN_RX_PIN           UMCUB_PIN('D', 0, 9) */
/* Internal loopback (no transceiver needed) - for self-test only. */
/* #define UMCUB_CFG_CAN_LOOPBACK         0 */
/* Controller driver: UMCUB_DRIVER_PORT (on-chip FDCAN/bxCAN) or
 * UMCUB_DRIVER_BOARD (external controller, e.g. MCP2515/MCP2518FD on SPI):
 * then boards/<b>/umcub_board.c implements umcub_port_can_init/deinit/send/recv
 * (port/include/umcub_port_can.h) and ISO-TP + SMP run on top of it. */
/* #define UMCUB_CFG_CAN_DRIVER           UMCUB_DRIVER_PORT */

/* --- Ethernet: SMP over UDP, minimal IPv4 stack with DHCP --------------- */
/* #define UMCUB_CFG_TRANSPORT_ETH        0 */
/* #define UMCUB_CFG_ETH_DHCP             1 */
/* Used when DHCP is off, or as fallback after UMCUB_CFG_ETH_DHCP_TIMEOUT_MS. */
/* #define UMCUB_CFG_ETH_IP               UMCUB_IP4(192, 168, 1, 50) */
/* #define UMCUB_CFG_ETH_NETMASK          UMCUB_IP4(255, 255, 255, 0) */
/* #define UMCUB_CFG_ETH_GATEWAY          UMCUB_IP4(192, 168, 1, 1) */
/* #define UMCUB_CFG_ETH_DHCP_TIMEOUT_MS  10000 */
/* MAC: 0 = derived from the MCU unique ID (locally administered). */
/* #define UMCUB_CFG_ETH_MAC              0 */
/* #define UMCUB_CFG_ETH_SMP_PORT         1337 */
/* #define UMCUB_CFG_ETH_PHY_ADDR         0 */
/* RMII pins as an initializer list of UMCUB_PIN(...). */
/* #define UMCUB_CFG_ETH_RMII_PINS        UMCUB_PIN('A', 1, 11), ... */
/* MAC driver: UMCUB_DRIVER_PORT (on-chip MAC + RMII PHY) or UMCUB_DRIVER_BOARD
 * (external MAC, e.g. ENC28J60/LAN9250 on SPI): boards/<b>/umcub_board.c then
 * implements umcub_port_eth_* (port/include/umcub_port_eth.h, raw frames) and
 * the IPv4/DHCP stack runs on top of it. */
/* #define UMCUB_CFG_ETH_DRIVER           UMCUB_DRIVER_PORT */

/* --- Board transport ------------------------------------------------------ */
/* Anything else (RS-485 with own framing, BLE module, W5500 UDP socket, ...):
 * boards/<b>/umcub_board.c defines
 *     const umcub_transport_t umcub_transport_user = { ... };
 * (transport/include/umcub_transport.h) - a stream (SMP serial framing) or a
 * packet transport (raw SMP packets). Polled with the built-in ones. */
/* #define UMCUB_CFG_TRANSPORT_USER       0 */

/* ======================================================================== */
/* Misc                                                                     */
/* ======================================================================== */

/* 0 none, 1 error, 2 warning, 3 info, 4 debug. Printed on the UART. */
/* #define UMCUB_CFG_LOG_LEVEL            3 */

/* Independent watchdog fed by the bootloader (timeout in ms, 0 = off). */
/* #define UMCUB_CFG_WATCHDOG_MS          0 */

/* RAM shared with the application (handoff struct, survives reset).
 * Must be outside of the application's .data/.bss. */
/* #define UMCUB_CFG_SHARED_RAM_ADDR      0x3800FF00 */

/* Where umcub_slot_*() in the application writes by default:
 * UMCUB_SLOT_SECONDARY, UMCUB_SLOT_PRIMARY, UMCUB_SLOT_INACTIVE. */
/* #define UMCUB_CFG_APP_WRITE_SLOT       UMCUB_SLOT_SECONDARY */

#endif /* UMCUB_CONFIG_H */
