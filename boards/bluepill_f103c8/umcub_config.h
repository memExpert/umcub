/*
 * umcub configuration: "Blue Pill" (STM32F103C8T6, 64 KiB flash, 20 KiB SRAM,
 * 8 MHz crystal, LED PC13). Minimal build: SMP over USART1 PA9/PA10 (the pins
 * of the ROM bootloader, so one USB-UART adapter serves both), overwrite mode.
 */
#ifndef UMCUB_CONFIG_H
#define UMCUB_CONFIG_H

#define UMCUB_CFG_MCU                   STM32F103xB
#define UMCUB_CFG_BOARD_TYPE            0x46103001      /* images must carry this (signed TLV) */
#define UMCUB_CFG_BOARD_REV             1

#define UMCUB_CFG_CLOCK_SOURCE          UMCUB_CLK_HSE
#define UMCUB_CFG_HSE_HZ                8000000

/* 64 KiB = bootloader 32 KiB + two 16 KiB slots (1 KiB pages). Overwrite:
 * the secondary slot is copied over the primary, no swap space needed. */
#ifndef UMCUB_CFG_UPGRADE_MODE
#define UMCUB_CFG_UPGRADE_MODE          UMCUB_MODE_OVERWRITE
#endif
#define UMCUB_CFG_IMAGE_HEADER_SIZE     0x200   /* VTOR: minimum alignment 128 words (PM0056 §4.4.4) */
#define UMCUB_CFG_TRAILER_RESERVE       0x400
#define UMCUB_CFG_BOOT_ADDR             0x08000000
#define UMCUB_CFG_BOOT_SIZE             UMCUB_KB(32)
#define UMCUB_CFG_IMG0_PRIMARY_ADDR     0x08008000
#define UMCUB_CFG_IMG0_PRIMARY_SIZE     UMCUB_KB(16)
#define UMCUB_CFG_IMG0_SECONDARY_ADDR   0x0800C000
#define UMCUB_CFG_IMG0_SECONDARY_SIZE   UMCUB_KB(16)

/* No user button: recovery on request from the application, without a
 * valid image, or by sending "b" during the first 300 ms. */
#define UMCUB_CFG_ENTRY_WAIT_MS         300

#define UMCUB_CFG_CMD_ENABLE            1
#define UMCUB_CFG_CMD_TABLE             UMCUB_CMD("a", UMCUB_CMD_BOOT_APP) \
                                        UMCUB_CMD("b", UMCUB_CMD_STAY)     \
                                        UMCUB_CMD("r", UMCUB_CMD_RESET)    \
                                        UMCUB_CMD("i", UMCUB_CMD_INFO)

#define UMCUB_CFG_SMP_MTU               512     /* 20 KiB SRAM */

#define UMCUB_CFG_TRANSPORT_UART        1
#define UMCUB_CFG_UART_INSTANCE         1
#define UMCUB_CFG_UART_BAUD             115200
#define UMCUB_CFG_UART_TX_PIN           UMCUB_PIN('A', 9, 0)
#define UMCUB_CFG_UART_RX_PIN           UMCUB_PIN('A', 10, 0)

/* bxCAN on PB9 (TX) / PB8 (RX) - needs a transceiver; not together with USB
 * (shared SRAM). Enable with UMCUB_CFG_TRANSPORT_CAN (tools/config/bluepill_can.h). */
#define UMCUB_CFG_CAN_TX_PIN            UMCUB_PIN('B', 9, 0)
#define UMCUB_CFG_CAN_RX_PIN            UMCUB_PIN('B', 8, 0)

#define UMCUB_CFG_LOG_LEVEL             3

#endif /* UMCUB_CONFIG_H */
