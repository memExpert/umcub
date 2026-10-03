/*
 * STM32F1 family defaults (applied after the board config, before the
 * generic defaults).
 */
#ifndef UMCUB_FAMILY_DEFAULTS_H
#define UMCUB_FAMILY_DEFAULTS_H

#define UMCUB_FAMILY_STM32F1            1

/* Flash page (erase unit), RM0008 §3.3.3 / PM0075 §2.3: 1 KiB on low- and
 * medium-density parts, 2 KiB on high-density, XL and connectivity lines. */
#if defined(STM32F100xB) || defined(STM32F101x6) || defined(STM32F101xB) || defined(STM32F102x6) || \
    defined(STM32F102xB) || defined(STM32F103x6) || defined(STM32F103xB)
#define UMCUB_FAMILY_UNIFORM_SECTOR     UMCUB_KB(1)
#else
#define UMCUB_FAMILY_UNIFORM_SECTOR     UMCUB_KB(2)
#endif
#define UMCUB_FAMILY_MIN_SECTOR         UMCUB_FAMILY_UNIFORM_SECTOR
/* Worst-case page erase time (DS5319 / PM0075: 20..40 ms). */
#define UMCUB_FAMILY_SECTOR_ERASE_MS    40
/* The flash programs half-words; 8 is the smallest MCUBOOT_BOOT_MAX_ALIGN
 * MCUboot supports, so the driver takes multiples of 8 bytes. */
#define UMCUB_FAMILY_WRITE_ALIGN        8

/* SRAM size (DS5319 / DS5792 ordering information). */
#if defined(STM32F103x6) || defined(STM32F101x6) || defined(STM32F102x6)
#define UMCUB_FAMILY_RAM_SIZE           UMCUB_KB(10)
#elif defined(STM32F103xB) || defined(STM32F101xB) || defined(STM32F102xB)
#define UMCUB_FAMILY_RAM_SIZE           UMCUB_KB(20)
#elif defined(STM32F100xB)
#define UMCUB_FAMILY_RAM_SIZE           UMCUB_KB(8)
#elif defined(STM32F103xE)
#define UMCUB_FAMILY_RAM_SIZE           UMCUB_KB(64)
#elif defined(STM32F103xG)
#define UMCUB_FAMILY_RAM_SIZE           UMCUB_KB(96)
#elif defined(STM32F105xC) || defined(STM32F107xC)
#define UMCUB_FAMILY_RAM_SIZE           UMCUB_KB(64)
#else
#error "umcub: STM32F1 part not listed in port/stm32f1/include/umcub_family_defaults.h"
#endif

#ifndef UMCUB_CFG_BOOT_ADDR
#define UMCUB_CFG_BOOT_ADDR             0x08000000
#endif
/* Last 256 bytes of SRAM (kept over reset; the application must not use them). */
#ifndef UMCUB_CFG_SHARED_RAM_ADDR
#define UMCUB_CFG_SHARED_RAM_ADDR       (0x20000000 + UMCUB_FAMILY_RAM_SIZE - 256)
#endif

#endif /* UMCUB_FAMILY_DEFAULTS_H */
