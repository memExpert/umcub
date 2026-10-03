/*
 * STM32H7 family defaults (applied after the board config, before the
 * generic defaults).
 */
#ifndef UMCUB_FAMILY_DEFAULTS_H
#define UMCUB_FAMILY_DEFAULTS_H

#define UMCUB_FAMILY_STM32H7            1
/* Single-bank-size parts (H7A3/B0 have 8K sectors) are not covered yet. */
#define UMCUB_FAMILY_UNIFORM_SECTOR     UMCUB_KB(128)
#define UMCUB_FAMILY_MIN_SECTOR         UMCUB_KB(128)
/* Flash program unit: one 256-bit flash word. */
#define UMCUB_FAMILY_WRITE_ALIGN        32

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
