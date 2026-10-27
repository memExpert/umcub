/*
 * umcub configuration: the minimum for an STM32F4. Copy it to
 * boards/<your_board>/umcub_config.h and change the lines marked "yours".
 * Everything else takes its default (config/umcub_config_defaults.h); all
 * options with their meaning are in config/umcub_config_template.h. The build
 * stops with a message when something does not fit (slots off sector
 * boundaries, pins without the alternate function, a clock the port cannot
 * use, ...).
 *
 * This example: STM32F411CE (512 KiB flash: sectors 4 x 16, 64, 3 x 128 KiB),
 * 25 MHz crystal, UART on USART1 PA9/PA10, recovery by a button on PA0.
 */
#ifndef UMCUB_CONFIG_H
#define UMCUB_CONFIG_H

/* --- the part (yours): CMSIS device define, clock ------------------------ */
#define UMCUB_CFG_MCU                   STM32F411xE
#define UMCUB_CFG_MCU_PART              "STM32F411CEU6"     /* on the chip: pins checked against its package */
#define UMCUB_CFG_CLOCK_SOURCE          UMCUB_CLK_HSE       /* or UMCUB_CLK_HSI: no crystal, no USB */
#define UMCUB_CFG_HSE_HZ                25000000

/* --- flash (yours): bootloader, then the two slots of the application ---- */
/* Whole sectors: bootloader in sectors 0-1, the slots in sectors 5 and 6.
 * Overwrite: the secondary slot is copied over the primary. The application
 * is linked for the primary slot (tools/host/umcub image info prints where). */
#define UMCUB_CFG_UPGRADE_MODE          UMCUB_MODE_OVERWRITE
#define UMCUB_CFG_BOOT_SIZE             UMCUB_KB(32)
#define UMCUB_CFG_IMG0_PRIMARY_ADDR     0x08020000
#define UMCUB_CFG_IMG0_PRIMARY_SIZE     UMCUB_KB(128)
#define UMCUB_CFG_IMG0_SECONDARY_ADDR   0x08040000
#define UMCUB_CFG_IMG0_SECONDARY_SIZE   UMCUB_KB(128)

/* --- how to get into the bootloader (yours) ------------------------------ */
/* A button (here PA0 to GND); the application can always ask
 * (umcub_enter_bootloader()), and without a valid image it stays anyway. */
#define UMCUB_CFG_ENTRY_GPIO            1
#define UMCUB_CFG_ENTRY_GPIO_PIN        UMCUB_PIN('A', 0)
#define UMCUB_CFG_ENTRY_GPIO_ACTIVE     0
#define UMCUB_CFG_ENTRY_GPIO_PULL       UMCUB_PULL_UP

/* --- transport (yours): here a UART, SMP for smpmgr / mcumgr ------------- */
/* UMCUB_PIN(port, pin): the alternate function comes from ST's pin data. */
#define UMCUB_CFG_TRANSPORT_UART        1
#define UMCUB_CFG_UART_INSTANCE         1
#define UMCUB_CFG_UART_BAUD             115200
#define UMCUB_CFG_UART_TX_PIN           UMCUB_PIN('A', 9)
#define UMCUB_CFG_UART_RX_PIN           UMCUB_PIN('A', 10)

/* For bring-up: boot log on the same UART (0 = off, the default). */
/* #define UMCUB_CFG_LOG_LEVEL            3 */

#endif /* UMCUB_CONFIG_H */
