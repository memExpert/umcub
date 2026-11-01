/*
 * STM32H7 family facts (behind the generated umcub_config.h). Option defaults: port/Kconfig.
 */
#ifndef UMCUB_FAMILY_FACTS_H
#define UMCUB_FAMILY_FACTS_H

#define UMCUB_FAMILY_STM32H7            1

/* Parts the port is verified for (RM0433, RM0399): 128 KiB sectors, 64 KiB
 * SRAM4 for the handoff. Not H72x/73x (16 KiB SRAM4), H7A3/B0/B3 (8 KiB
 * sectors) or H750 (128 KiB of flash in all). */
#if !(defined(STM32H742xx) || defined(STM32H743xx) || defined(STM32H753xx) || defined(STM32H745xx) || \
      defined(STM32H745xG) || defined(STM32H747xx) || defined(STM32H747xG) || defined(STM32H755xx) || \
      defined(STM32H757xx))
#error "umcub: this STM32H7 part is not verified with the port: check its flash sectors and SRAM4 (datasheet, reference manual), then add it to the list in port/stm32h7/include/umcub_family_facts.h (docs/porting.md, Adding a part)"
#endif
#define UMCUB_FAMILY_UNIFORM_SECTOR     UMCUB_KB(128)
#define UMCUB_FAMILY_MIN_SECTOR         UMCUB_KB(128)
/* Worst-case erase time of one sector (DS12919: 128 KiB, x64, max 4 s). */
#define UMCUB_FAMILY_SECTOR_ERASE_MS    4000
/* Flash program unit: one 256-bit flash word. */
#define UMCUB_FAMILY_WRITE_ALIGN        32

#if defined(STM32H745xx) || defined(STM32H755xx) || defined(STM32H747xx) || \
    defined(STM32H757xx) || defined(STM32H745xG) || defined(STM32H747xG)
#define UMCUB_FAMILY_DUALCORE           1
#define UMCUB_FAMILY_CORE2_PARTNO       0xC24   /* Cortex-M4 (SCB->CPUID PARTNO) */
#endif


#endif /* UMCUB_FAMILY_FACTS_H */
