/*
 * Constants and helper macros usable inside umcub_config.h.
 * Pure preprocessor - safe to include from C, assembly and linker scripts.
 */
#ifndef UMCUB_CONFIG_TYPES_H
#define UMCUB_CONFIG_TYPES_H

/* Core being built (dual-core parts). CMake passes UMCUB_CORE_CM7/CM4; IDE
 * projects (CubeIDE, Keil) usually only have the CMSIS CORE_CM7/CORE_CM4. */
#if !defined(UMCUB_CORE_CM7) && !defined(UMCUB_CORE_CM4)
#if defined(CORE_CM4)
#define UMCUB_CORE_CM4 1
#elif defined(CORE_CM7)
#define UMCUB_CORE_CM7 1
#endif
#endif

#define UMCUB_KB(n)                 ((n) * 1024)
#define UMCUB_MB(n)                 ((n) * 1024 * 1024)

/* GPIO pin: port letter ('A'..'K'), pin number 0..15, alternate function 0..15. */
#define UMCUB_PIN(port, pin, af)    ((((port) - 'A') << 8) | ((pin) << 4) | (af))
/* No pin (optional pins such as UMCUB_CFG_UART_DE_PIN). */
#define UMCUB_PIN_NONE              0xFFFFFFFFu
#define UMCUB_PIN_PORT(p)           (((p) >> 8) & 0xF)
#define UMCUB_PIN_NUM(p)            (((p) >> 4) & 0xF)
#define UMCUB_PIN_AF(p)             ((p) & 0xF)

#define UMCUB_PULL_NONE             0
#define UMCUB_PULL_UP               1
#define UMCUB_PULL_DOWN             2

/* IPv4 address in host byte order. */
#define UMCUB_IP4(a, b, c, d)       ((((a) & 0xFFu) << 24) | (((b) & 0xFFu) << 16) | \
                                     (((c) & 0xFFu) << 8) | ((d) & 0xFFu))

/* UMCUB_CFG_DUALCORE_MODE */
#define UMCUB_DUALCORE_NONE         0
#define UMCUB_DUALCORE_SINGLE_BOOT  1
#define UMCUB_DUALCORE_PER_CORE     2

/* UMCUB_CFG_CAN_DRIVER / UMCUB_CFG_ETH_DRIVER */
#define UMCUB_DRIVER_PORT           0   /* on-chip controller, driver from port/<family>/ */
#define UMCUB_DRIVER_BOARD          1   /* external controller (SPI, ...), driver in boards/<b>/umcub_board.c */

/* UMCUB_CFG_<transport>_LINK */
#define UMCUB_LINK_PLAIN            0   /* SMP / text as today (point to point) */
#define UMCUB_LINK_ADDRESSED        1   /* umcub link frames: node addressing, discovery */
#define UMCUB_LINK_SECURE           2   /* + host authentication, per-frame MAC */

/* MCUboot image TLV (protected, signed) carrying UMCUB_CFG_BOARD_TYPE (u32 LE). */
#define UMCUB_TLV_BOARD_TYPE        0xA0

/* UMCUB_CFG_CLOCK_SOURCE */
#define UMCUB_CLK_HSI               0
#define UMCUB_CLK_HSE               1

/* UMCUB_CFG_UPGRADE_MODE */
#define UMCUB_MODE_OVERWRITE        1
#define UMCUB_MODE_SWAP_SCRATCH     2
#define UMCUB_MODE_SWAP_MOVE        3
#define UMCUB_MODE_SWAP_OFFSET      4
#define UMCUB_MODE_DIRECT_XIP       5
#define UMCUB_MODE_DIRECT_XIP_REVERT 6

/* UMCUB_CFG_SIGNATURE */
#define UMCUB_SIGN_NONE             0
#define UMCUB_SIGN_EC256            1

/* UMCUB_CFG_APP_WRITE_SLOT */
#define UMCUB_SLOT_PRIMARY          0
#define UMCUB_SLOT_SECONDARY        1
#define UMCUB_SLOT_INACTIVE         2

/* Text commands (UMCUB_CFG_CMD_TABLE): UMCUB_CMD("text", action) entries. */
#define UMCUB_CMD(text, action)     { (text), (action) },
#define UMCUB_CMD_BOOT_APP          1   /* leave the bootloader, start the application */
#define UMCUB_CMD_STAY              2   /* stay in recovery (e.g. during UMCUB_CFG_ENTRY_WAIT_MS) */
#define UMCUB_CMD_RESET             3   /* plain reset */
#define UMCUB_CMD_INFO              4   /* print bootloader / image versions and states */
/* With arguments (always need Enter): <image> <slot> [<offset> <length>] */
#define UMCUB_CMD_VERIFY            5   /* full image validation: "ok valid" / "bad" */
#define UMCUB_CMD_HASH              6   /* SHA-256 of a slot range (default: whole image) */
#define UMCUB_CMD_READ              7   /* hex dump, needs UMCUB_CFG_READBACK */
#define UMCUB_CMD_USER(n)           (0x100 + (n))   /* handled by umcub_cmd_user() */

/* STM32H7 power supply (UMCUB_CFG_PWR_SUPPLY), see RM0399 "PWR_CR3". */
#define UMCUB_H7_SUPPLY_LDO                 1
#define UMCUB_H7_SUPPLY_DIRECT_SMPS         2
#define UMCUB_H7_SUPPLY_SMPS_1V8_LDO        3
#define UMCUB_H7_SUPPLY_SMPS_2V5_LDO        4
#define UMCUB_H7_SUPPLY_SMPS_1V8_EXT_LDO    5
#define UMCUB_H7_SUPPLY_SMPS_2V5_EXT_LDO    6
#define UMCUB_H7_SUPPLY_SMPS_1V8_EXT        7
#define UMCUB_H7_SUPPLY_SMPS_2V5_EXT        8
#define UMCUB_H7_SUPPLY_EXTERNAL            9

#endif /* UMCUB_CONFIG_TYPES_H */
