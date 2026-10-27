/*
 * STM32H7 family defaults (applied after the board config, before the
 * generic defaults).
 */
#ifndef UMCUB_FAMILY_DEFAULTS_H
#define UMCUB_FAMILY_DEFAULTS_H

#define UMCUB_FAMILY_STM32H7            1

/* Parts the port is verified for (RM0433, RM0399): 128 KiB sectors, 64 KiB
 * SRAM4 for the handoff. Not H72x/73x (16 KiB SRAM4), H7A3/B0/B3 (8 KiB
 * sectors) or H750 (128 KiB of flash in all). */
#if !(defined(STM32H742xx) || defined(STM32H743xx) || defined(STM32H753xx) || defined(STM32H745xx) || \
      defined(STM32H745xG) || defined(STM32H747xx) || defined(STM32H747xG) || defined(STM32H755xx) || \
      defined(STM32H757xx))
#error "umcub: this STM32H7 part is not verified with the port: check its flash sectors and SRAM4 (datasheet, reference manual), then add it to the list in port/stm32h7/include/umcub_family_defaults.h (docs/porting.md, Adding a part)"
#endif
#define UMCUB_FAMILY_UNIFORM_SECTOR     UMCUB_KB(128)
#define UMCUB_FAMILY_MIN_SECTOR         UMCUB_KB(128)
/* Worst-case erase time of one sector (DS12919: 128 KiB, x64, max 4 s). */
#define UMCUB_FAMILY_SECTOR_ERASE_MS    4000
/* Flash program unit: one 256-bit flash word. */
#define UMCUB_FAMILY_WRITE_ALIGN        32
/* The last sector of a slot stays free of the image (and its TLVs): SMP upload erases it
 * again after the last chunk (mcu-tools/mcuboot#2920, UMCUB_CFG_ERASE_PROGRESSIVELY). */
#ifndef UMCUB_CFG_TRAILER_RESERVE
#define UMCUB_CFG_TRAILER_RESERVE       (UMCUB_FAMILY_UNIFORM_SECTOR + 0x400)   /* + the TLVs */
#endif

#if defined(STM32H745xx) || defined(STM32H755xx) || defined(STM32H747xx) || \
    defined(STM32H757xx) || defined(STM32H745xG) || defined(STM32H747xG)
#define UMCUB_FAMILY_DUALCORE           1
#define UMCUB_FAMILY_CORE2_PARTNO       0xC24   /* Cortex-M4 (SCB->CPUID PARTNO) */
#endif

#ifndef UMCUB_CFG_BOOT_ADDR
#define UMCUB_CFG_BOOT_ADDR             0x08000000
#endif
#ifndef UMCUB_CFG_BOOT_SIZE
#define UMCUB_CFG_BOOT_SIZE             UMCUB_KB(128)
#endif
#ifndef UMCUB_CFG_PWR_SUPPLY
#define UMCUB_CFG_PWR_SUPPLY            UMCUB_H7_SUPPLY_LDO
#endif
/* Last 256 bytes of SRAM4 (D3 domain, reachable by both cores, kept over reset). */
#ifndef UMCUB_CFG_SHARED_RAM_ADDR
#define UMCUB_CFG_SHARED_RAM_ADDR       0x3800FF00
#endif

#endif /* UMCUB_FAMILY_DEFAULTS_H */
