/*
 * umcub configuration: WeAct "Black Pill" (STM32F411CEU6, 512 KiB flash,
 * 128 KiB SRAM, 25 MHz crystal, LED PC13, KEY button PA0). Minimal build:
 * SMP over USART1 PA9/PA10 (the pins of the ROM bootloader), overwrite mode.
 */
#ifndef UMCUB_CONFIG_H
#define UMCUB_CONFIG_H

#define UMCUB_CFG_MCU                   STM32F411xE
#define UMCUB_CFG_MCU_PART              "STM32F411CEU6"         /* UFQFPN48 */
#define UMCUB_CFG_BOARD_TYPE            0x46411001      /* images must carry this (signed TLV) */
#define UMCUB_CFG_BOARD_REV             1

#define UMCUB_CFG_CLOCK_SOURCE          UMCUB_CLK_HSE
#define UMCUB_CFG_HSE_HZ                25000000

/* Sectors 4 x 16, 64, 3 x 128 KiB: bootloader in sectors 0-1 (32 KiB), the
 * slots in sectors 5 and 6 (128 KiB each); sectors 2-4 and 7 stay free (e.g.
 * application data). Overwrite: the secondary slot is copied over the primary. */
#ifndef UMCUB_CFG_UPGRADE_MODE
#define UMCUB_CFG_UPGRADE_MODE          UMCUB_MODE_OVERWRITE
#endif
#define UMCUB_CFG_IMAGE_HEADER_SIZE     0x200   /* VTOR: minimum alignment 128 words (PM0214 §4.4.4) */
#define UMCUB_CFG_BOOT_ADDR             0x08000000
#define UMCUB_CFG_BOOT_SIZE             UMCUB_KB(32)
#define UMCUB_CFG_IMG0_PRIMARY_ADDR     0x08020000
#define UMCUB_CFG_IMG0_PRIMARY_SIZE     UMCUB_KB(128)
#define UMCUB_CFG_IMG0_SECONDARY_ADDR   0x08040000
#define UMCUB_CFG_IMG0_SECONDARY_SIZE   UMCUB_KB(128)

/* KEY (PA0 to GND) held at reset: stay in recovery. */
#define UMCUB_CFG_ENTRY_GPIO            1
#define UMCUB_CFG_ENTRY_GPIO_PIN        UMCUB_PIN('A', 0)
#define UMCUB_CFG_ENTRY_GPIO_ACTIVE     0
#define UMCUB_CFG_ENTRY_GPIO_PULL       UMCUB_PULL_UP

#define UMCUB_CFG_STATUS_LED            1       /* PC13, lit low: blinks while in the bootloader */
#define UMCUB_CFG_STATUS_LED_PIN        UMCUB_PIN('C', 13)
#define UMCUB_CFG_STATUS_LED_ACTIVE     0

#define UMCUB_CFG_CMD_ENABLE            1
#define UMCUB_CFG_CMD_TABLE             UMCUB_CMD("a", UMCUB_CMD_BOOT_APP) \
                                        UMCUB_CMD("b", UMCUB_CMD_STAY)     \
                                        UMCUB_CMD("r", UMCUB_CMD_RESET)    \
                                        UMCUB_CMD("i", UMCUB_CMD_INFO)     \
                                        UMCUB_DEFAULT_CMD_INSPECT   /* verify, hash: without SMP */

#define UMCUB_CFG_TRANSPORT_UART        1
#define UMCUB_CFG_UART_INSTANCE         1
#define UMCUB_CFG_UART_BAUD             115200
#define UMCUB_CFG_UART_TX_PIN           UMCUB_PIN('A', 9)
#define UMCUB_CFG_UART_RX_PIN           UMCUB_PIN('A', 10)

#define UMCUB_CFG_LOG_LEVEL             3

#endif /* UMCUB_CONFIG_H */
