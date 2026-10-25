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
 * UMCUB_PIN(port, pin) (the alternate function filled in from ST's pin data
 * when CMake configures) or UMCUB_PIN(port, pin, af), UMCUB_IP4(a, b, c, d).
 */
#ifndef UMCUB_CONFIG_H
#define UMCUB_CONFIG_H

/* ======================================================================== */
/* Target                                                                   */
/* ======================================================================== */

/* CMSIS device define of the MCU (selects family port, startup, CPU flags). */
#define UMCUB_CFG_MCU                     STM32H755xx
/* Part number as printed on the chip (or the CubeMX name): the build then also
 * refuses pins this package does not bring out (ST pin data). Build check
 * only, nothing in the image. "" = only the pins of the whole line are known. */
/* #define UMCUB_CFG_MCU_PART             "STM32H755ZIT6" */

/* Dual-core handling (only for dual-core parts such as STM32H745/H755/H747):
 *   UMCUB_DUALCORE_NONE        - single core part or second core unused
 *   UMCUB_DUALCORE_SINGLE_BOOT - one bootloader on the main core manages two
 *                                images (image 0 = main core, image 1 = second
 *                                core) and releases the second core
 *   UMCUB_DUALCORE_PER_CORE    - every core runs its own bootloader instance
 *                                with its own image(s) and transports        */
/* #define UMCUB_CFG_DUALCORE_MODE        UMCUB_DUALCORE_NONE */
/* PER_CORE, main core: how long to wait for the second core's bootloader
 * before the clocks are handed to the application (then they stay running). */
/* #define UMCUB_CFG_CORE2_DONE_TIMEOUT_MS 3000 */

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
/* Bytes at the end of every slot the application must not use (MCUboot
 * trailer; imgtool --slot-size checks it). Swap modes need room for the swap
 * status, overwrite only a few dozen bytes: one flash page is plenty. */
/* #define UMCUB_CFG_TRAILER_RESERVE      0x2000 */
/* SMP upload erases the slot sector by sector as the chunks arrive (1) or the
 * whole slot on the first chunk (0; seconds on big sectors, hosts may time
 * out). MCUboot up to v2.5.0 erases the last sector of the slot again after
 * the upload (mcu-tools/mcuboot#2920): with 1, UMCUB_CFG_TRAILER_RESERVE must
 * cover that sector, so a slot of one sector needs 0. */
/* #define UMCUB_CFG_ERASE_PROGRESSIVELY  1 */

/* ======================================================================== */
/* MCUboot                                                                  */
/* ======================================================================== */

/* UMCUB_MODE_OVERWRITE, UMCUB_MODE_SWAP_SCRATCH, UMCUB_MODE_SWAP_MOVE,
 * UMCUB_MODE_SWAP_OFFSET, UMCUB_MODE_DIRECT_XIP, UMCUB_MODE_DIRECT_XIP_REVERT */
/* #define UMCUB_CFG_UPGRADE_MODE         UMCUB_MODE_OVERWRITE */

/* Signature: UMCUB_SIGN_EC256 (ECDSA-P256, tinycrypt) or UMCUB_SIGN_NONE:
 * images carry a SHA-256 only - damaged ones are refused, but anybody's are
 * booted (no authenticity; about 4.6 K smaller, CMake warns). Only for closed
 * devices on small parts. */
/* #define UMCUB_CFG_SIGNATURE            UMCUB_SIGN_EC256 */

/* Refuse images with a lower version than the running one. */
/* #define UMCUB_CFG_DOWNGRADE_PREVENTION 0 */

/* Validate the primary slot (hash + signature, board type, reset vector) on
 * every boot (recommended). 0 boots faster (about 0.6 s on a 72 MHz
 * Cortex-M3, ~80 ms per image on the H7): updates through the secondary slot
 * are still validated before they are installed, but an image written
 * straight into the primary slot (SMP or lite recovery upload, a debugger)
 * then boots unchecked. Refused at build time together with SMP and a board
 * type, UMCUB_LINK_SECURE or encrypted images. (MCUboot's "validate once"
 * exists only for its single-slot loader, not for these modes.) */
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
/* #define UMCUB_CFG_ENTRY_GPIO_PIN       UMCUB_PIN('C', 13) */
/* #define UMCUB_CFG_ENTRY_GPIO_ACTIVE    1 */
/* #define UMCUB_CFG_ENTRY_GPIO_PULL      UMCUB_PULL_NONE */
/* Listen this long on every boot for an SMP request or a text command such as
 * "b" before starting the application (0 = don't wait). Every enabled
 * transport listens and is switched off again after the window, so pick the
 * length for the slowest one you want to use: UART / CAN answer at once;
 * Ethernet needs the link (~2 s) and a DHCP lease (or a static address);
 * USB needs the host to enumerate and open the device (seconds). USB attaches
 * and detaches on every boot then, and a device that vanishes in the middle
 * of enumeration upsets some host controllers: with USB, prefer 0 or a window
 * of several seconds. Each boot is delayed by the window. */
/* #define UMCUB_CFG_ENTRY_WAIT_MS        0 */
/* Leave recovery mode (reset) after this much inactivity (0 = never). */
/* #define UMCUB_CFG_RECOVERY_TIMEOUT_MS  0 */
/* Status LED, lit while the bootloader listens (wait window, recovery) -
 * shows what the log would say when there is none (about 200 bytes):
 *   slow blink (1 Hz)   in the bootloader, waiting
 *   fast flicker        data arriving
 *   N blinks + pause    a transport did not come up (init failed: no
 *                       transceiver, no PHY clock, ...), N = UMCUB_TRANSPORT_*:
 *                       1 UART, 2 USB, 4 CAN, 5 Ethernet, 7 board transport
 * Untouched on a plain boot; back in reset state before the application. */
/* #define UMCUB_CFG_STATUS_LED           0 */
/* #define UMCUB_CFG_STATUS_LED_PIN       UMCUB_PIN('C', 13) */
/* #define UMCUB_CFG_STATUS_LED_ACTIVE    1 */

/* ======================================================================== */
/* Recovery protocols                                                       */
/* ======================================================================== */

/* SMP (MCUboot serial recovery: mcumgr / smpmgr) on UART, USB CDC, CAN, UDP
 * and the board transport. 0 leaves boot_serial, zcbor and the SMP framing
 * out (about 10 K): images then come through the lite upload protocol below,
 * USB DFU, the application (umcub_slot_*) or the board's own protocol. */
/* #define UMCUB_CFG_SMP_ENABLE           1 */
/* Largest SMP packet (decoded); every SMP buffer is this size (RAM only).
 * Default 1472 with Ethernet (a full UDP datagram, what smpmgr sends), else
 * 1024. Hosts must not send more: smpmgr --mtu / --line-buffers. */
/* #define UMCUB_CFG_SMP_MTU              1024 */  /* at least 512 */
/* Answer MCUMGR_PARAMETERS (os group, id 6) with {buf_size: UMCUB_CFG_SMP_MTU,
 * buf_count: 1}: smpmgr then sizes serial packets itself (no --line-buffers).
 * MCUboot's own command (MCUBOOT_BOOT_MGMT_MCUMGR_PARAMS). About 130 bytes. */
/* #define UMCUB_CFG_SMP_PARAMS           1 */
/* Where an SMP upload goes. 0: "image" N is image N, written straight into
 * its primary slot (MCUboot serial recovery: replaced at once, no revert).
 * 1: "image" addresses slots - 0/1 image 0 primary, 2 image 0 secondary,
 * 3 image 1 primary, 4 image 1 secondary; an image in the secondary slot is
 * installed by the next boot after "image state-write" marks it (test boot,
 * revert in the swap modes). Always on in the direct-xip modes. Brings
 * MCUboot's image state ("image state-write", confirmed / pending flags in
 * "image state-read"; not in direct-xip), the image hash in "state-read" and
 * "image slot-info" along, about 2.4 K; without it "state-read" lists
 * versions only. */
/* #define UMCUB_CFG_SMP_DIRECT_UPLOAD    0 */
/* Built-in lite upload protocol (about 1 K plus the slot writer; host tool
 * tools/host/umcub_lite.py): begin / data / end frames with CRC on the same
 * transports, written with umcub_slot_* (header check, test or permanent
 * mark, in-place decryption of encrypted images). Default: on without SMP. */
/* #define UMCUB_CFG_LITE_UPLOAD          (1 - UMCUB_CFG_SMP_ENABLE) */
/* Board protocol: frames that are neither SMP, lite nor a text command go to
 * umcub_proto_user() in boards/<b>/umcub_board.c (packets of packet
 * transports and umcub link DATA payloads; on stream transports lines
 * 0x05 0x0D <base64> \n). Answer with umcub_proto_reply(); umcub_slot_* is
 * available to write images. */
/* #define UMCUB_CFG_PROTO_USER           0 */

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
/* Encrypted images (MCUboot ECIES-P256, AES-128-CTR) with the device key
 * (CMake UMCUB_DEVICE_KEY): image files and transfers do not show the
 * firmware. umcub_sign_image() / tools/host/umcub_image.py encrypt automatically.
 * Installed from the secondary slot, the image is decrypted while it is
 * copied; uploaded over SMP into the primary slot, it is decrypted in place
 * after the last chunk. Readback (UMCUB_CFG_READBACK) then answers only inside
 * an encrypted umcub link session (UMCUB_LINK_SECURE + UMCUB_CFG_LINK_ENCRYPT),
 * never over USB DFU. Not available in the direct-xip modes. */
/* #define UMCUB_CFG_ENCRYPT_IMAGES       0 */
/* In-place decryption of an encrypted SMP upload into the primary slot; costs
 * one flash sector of RAM (default 1, 0 on the H7 CM4 bootloader). Without it
 * encrypted images go through the secondary slot (USB DFU, the application,
 * or SMP with UMCUB_CFG_SMP_DIRECT_UPLOAD to image 2); an encrypted image
 * uploaded into the primary slot stays invalid. */
/* #define UMCUB_CFG_ENC_INPLACE          1 */
/* Swap modes: during an update MCUboot keeps the AES keys of both images in
 * the slot trailers until the swap is over (resume after a reset, revert).
 * 1 stores the encrypted key TLVs there instead (MCUBOOT_SWAP_SAVE_ENCTLV):
 * flash never holds a plain image key, it is unwrapped with the device key
 * when needed. Signing passes --save-enctlv (bigger trailer). */
/* #define UMCUB_CFG_SWAP_SAVE_ENCTLV     1 */
/* SMP group id of verify (0) / hash (1) / read (2) for host software. */
/* #define UMCUB_CFG_SMP_INSPECT_GROUP    100 */

/* ======================================================================== */
/* Transports                                                               */
/* ======================================================================== */

/* --- UART: SMP serial (mcumgr/smpmgr --conntype serial) ----------------- */
/* #define UMCUB_CFG_TRANSPORT_UART       1 */
/* #define UMCUB_CFG_UART_INSTANCE        3            (USART3) */
/* #define UMCUB_CFG_UART_BAUD            115200 */
/* #define UMCUB_CFG_UART_TX_PIN          UMCUB_PIN('D', 8) */
/* #define UMCUB_CFG_UART_RX_PIN          UMCUB_PIN('D', 9) */
/* RS485 (half duplex): driver-enable pin of the transceiver (DE, usually tied
 * to /RE), UMCUB_PIN_NONE = plain UART. STM32H7: give the USART's RTS/DE pin
 * with its AF number for hardware DE timing (UMCUB_PIN('D', 12, 7)); a pin
 * without an AF (or AF 0) is switched by software; STM32F1 always by software. */
/* #define UMCUB_CFG_UART_DE_PIN          UMCUB_PIN_NONE */
/* #define UMCUB_CFG_UART_DE_ACTIVE       1     (1 = DE high while sending) */
/* Pause before answering, so the host has released the bus (ms). */
/* #define UMCUB_CFG_UART_TURNAROUND_MS   0 */
/* Transmit through a RAM ring of this many bytes (power of two, e.g. 512),
 * drained by the UART interrupt: writes (log, SMP answers) return at once and
 * the boot log overlaps the signature check instead of stalling it (~5 ms per
 * 60-character line at 115200). 0 = blocking writes (default). Costs the RAM
 * and ~0.1-0.2 K of code; the ring is drained before the jump, but on a hang,
 * fault or reset the last lines can stay in RAM and never reach the UART -
 * keep 0 when the log is used to find where the bootloader stops. Not with
 * RS485 (UMCUB_CFG_UART_DE_PIN). */
/* #define UMCUB_CFG_UART_TX_RING         0 */

/* --- USB device (tinyUSB): CDC-ACM with SMP and/or DFU ------------------ */
/* #define UMCUB_CFG_TRANSPORT_USB_CDC    0 */
/* #define UMCUB_CFG_TRANSPORT_USB_DFU    0 */
/* #define UMCUB_CFG_USB_VID              0x1209 */
/* #define UMCUB_CFG_USB_PID              0x0001 */
/* #define UMCUB_CFG_USB_MANUFACTURER     "umcub" */
/* #define UMCUB_CFG_USB_PRODUCT          "umcub bootloader" */
/* DFU: alt setting N = image N, downloaded into its secondary slot (also in
 * direct-xip) and marked pending after manifestation.                     */

/* --- CAN / FDCAN: SMP over ISO-TP-lite ---------------------------------- */
/* #define UMCUB_CFG_TRANSPORT_CAN        0 */
/* #define UMCUB_CFG_CAN_INSTANCE         1 */
/* #define UMCUB_CFG_CAN_BITRATE          500000 */
/* #define UMCUB_CFG_CAN_FD               0     (1 = CAN-FD frames, 64 byte) */
/* #define UMCUB_CFG_CAN_DATA_BITRATE     2000000 */
/* #define UMCUB_CFG_CAN_RX_ID            0x7C0 (host -> device) */
/* #define UMCUB_CFG_CAN_TX_ID            0x7C8 (device -> host) */
/* #define UMCUB_CFG_CAN_EXT_ID           0 */
/* #define UMCUB_CFG_CAN_TX_PIN           UMCUB_PIN('D', 1) */
/* #define UMCUB_CFG_CAN_RX_PIN           UMCUB_PIN('D', 0) */
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
/* #define UMCUB_CFG_ETH_DHCP_TIMEOUT_MS  10000 */
/* MAC: 0 = derived from the MCU unique ID (locally administered). */
/* #define UMCUB_CFG_ETH_MAC              0 */
/* #define UMCUB_CFG_ETH_SMP_PORT         1337 */
/* #define UMCUB_CFG_ETH_PHY_ADDR         0 */
/* RMII pins as an initializer list of UMCUB_PIN(...). */
/* #define UMCUB_CFG_ETH_RMII_PINS        UMCUB_PIN('A', 1), ... */
/* MAC driver: UMCUB_DRIVER_PORT (on-chip MAC + RMII PHY) or UMCUB_DRIVER_BOARD
 * (external MAC, e.g. ENC28J60/LAN9250 on SPI): boards/<b>/umcub_board.c then
 * implements umcub_port_eth_* (port/include/umcub_port_eth.h, raw frames) and
 * the IPv4/DHCP stack runs on top of it. */
/* #define UMCUB_CFG_ETH_DRIVER           UMCUB_DRIVER_PORT */

/* --- Board transport ------------------------------------------------------ */
/* Anything else (RS-485 with own framing, BLE module, W5500 UDP socket, ...):
 * boards/<b>/umcub_board.c defines
 *     const umcub_transport_t umcub_transport_user = { ... };
 * (src/transport/include/umcub_transport.h) - a stream (SMP serial framing) or a
 * packet transport (raw SMP packets). Polled with the built-in ones. */
/* #define UMCUB_CFG_TRANSPORT_USER       0 */

/* ======================================================================== */
/* umcub link: several devices on one bus                                   */
/* ======================================================================== */

/* Per transport: UMCUB_LINK_PLAIN (default; SMP and text as is, one device per
 * line), UMCUB_LINK_ADDRESSED (only umcub link frames: node address, discovery,
 * standard SMP inside; host side tools/host/umcub_link.py, which also serves
 * mcumgr/smpmgr), UMCUB_LINK_SECURE (additionally host authentication and a MAC
 * on every frame). */
/* #define UMCUB_CFG_UART_LINK            UMCUB_LINK_PLAIN */
/* #define UMCUB_CFG_USB_CDC_LINK         UMCUB_LINK_PLAIN */
/* #define UMCUB_CFG_CAN_LINK             UMCUB_LINK_PLAIN */
/* #define UMCUB_CFG_ETH_LINK             UMCUB_LINK_PLAIN */
/* #define UMCUB_CFG_USER_LINK            UMCUB_LINK_PLAIN */

/* UMCUB_LINK_SECURE: a host must prove it holds the admin key (ECDSA over a
 * fresh challenge) before the bootloader executes anything; the session keys
 * come from ECDH with the device key, every frame then carries a MAC and a
 * strictly increasing sequence number. Keys: CMake UMCUB_DEVICE_KEY (EC P-256
 * private, embedded) and UMCUB_HOST_KEY (the admin key; only its public half
 * is embedded). */
/* Encrypt session payloads (AES-128-CTR) in addition to the MAC. */
/* #define UMCUB_CFG_LINK_ENCRYPT         0 */
/* SECURE needs UMCUB_CFG_VALIDATE_PRIMARY and, with the watchdog on,
 * UMCUB_CFG_WATCHDOG_MS >= 2000 (an AUTH costs two ECC operations). */
/* Keep SECURE transports closed while flash readout protection is off (RDP
 * level 0): without it the device key can be read out with a debugger. Set 0
 * only for development. */
/* #define UMCUB_CFG_LINK_REQUIRE_RDP     1 */
/* A session ends after this long without a valid frame. */
/* #define UMCUB_CFG_LINK_SESSION_MS      60000 */
/* USB DFU cannot authenticate the host. With any SECURE transport it is
 * refused at build time unless this is set. */
/* #define UMCUB_CFG_USB_DFU_ALLOW_UNAUTH 0 */
/* A PLAIN transport next to a SECURE one gives unauthenticated access through
 * that transport; the build warns unless this is set. */
/* #define UMCUB_CFG_LINK_MIXED_OK        0 */

/* ======================================================================== */
/* Board identity                                                           */
/* ======================================================================== */

/* Board type (product id, u32) and hardware revision (u16). Reported to the
 * host (text command "i", SMP) and to the application (umcub_boot_info()).
 * Board type != 0: every image must carry the same value in its signed TLV
 * UMCUB_TLV_BOARD_TYPE (umcub_sign_image() / tools/host/umcub_image.py add it),
 * otherwise MCUboot rejects it before installing - firmware of another product
 * can never be booted, even when it is signed with the same key. */
/* #define UMCUB_CFG_BOARD_TYPE           0 */
/* #define UMCUB_CFG_BOARD_REV            0 */
/* Reject an image whose reset vector lies outside the slot it runs from (built
 * for another image / core, or uploaded with the wrong image number), before
 * MCUboot validates it. About 200 bytes. */
/* #define UMCUB_CFG_IMAGE_VECTOR_CHECK   1 */

/* The node address on a shared bus, in this order: from the application
 * (umcub_set_node_address(), kept over resets, not over power-off), from
 * UMCUB_CFG_NODE_ADDR_FLASH, or from the board hook
 * bool umcub_board_node_address(uint16_t *addr) in umcub_board.c (DIP switch,
 * EEPROM, ...); 0 = unassigned. */
/* Address of a 32-bit word in flash holding the node address as
 * UMCUB_NODE_ADDR_WORD(addr) (umcub.h: addr | ~addr << 16), e.g. a field of
 * the application's own settings structure: the address is then known right
 * after power-up, before the application has run. Must lie outside the image
 * slots (updates rewrite them). 0 = not used. */
/* #define UMCUB_CFG_NODE_ADDR_FLASH      0 */

/* ======================================================================== */
/* Misc                                                                     */
/* ======================================================================== */

/* Boot log on the UART: 0 off (default), 1 error, 2 warning, 3 info,
 * 4 debug. For bring-up and debugging; level 3 costs about 3.5 K. */
/* #define UMCUB_CFG_LOG_LEVEL            0 */

/* EXPERIMENTAL - link-time optimisation of the bootloader (-flto): 3-4 K
 * smaller on Cortex-M3, about 7 K on the H7. Not part of the verified
 * configurations: inlining makes stack frames much larger (check the stack
 * on your part), and weak functions overridden in boards/<b>/umcub_board.c
 * should be checked. Use at your own risk; CMake -DUMCUB_LTO=ON/OFF overrides
 * this for one build tree. */
/* #define UMCUB_CFG_LTO                  0 */

/* Independent watchdog fed by the bootloader (timeout in ms, 0 = off); at least
 * 1.5 x the erase time of one sector (H7 6 s, F4 3 s, F1 60 ms). */
/* #define UMCUB_CFG_WATCHDOG_MS          0 */

/* RAM shared with the application (handoff struct, survives reset).
 * Must be outside of the application's .data/.bss. */
/* #define UMCUB_CFG_SHARED_RAM_ADDR      0x3800FF00 */

/* Where umcub_slot_*() in the application writes by default:
 * UMCUB_SLOT_SECONDARY, UMCUB_SLOT_PRIMARY, UMCUB_SLOT_INACTIVE. */
/* #define UMCUB_CFG_APP_WRITE_SLOT       UMCUB_SLOT_SECONDARY */

#endif /* UMCUB_CONFIG_H */
