/*
 * STM32G4 family facts (behind the generated umcub_config.h). Option defaults: port/Kconfig.
 */
#ifndef UMCUB_FAMILY_FACTS_H
#define UMCUB_FAMILY_FACTS_H

#define UMCUB_FAMILY_STM32G4            1

/* Flash page (erase unit), RM0440 §3.3.1 Tables 7 and 8, in the bank mode the
 * configuration sets (UMCUB_CFG_FLASH_DUAL_BANK = the option bit DBANK, the
 * factory setting on the category 3 parts): 2 KiB in dual-bank mode, bank 2
 * from 0x08040000 whatever the flash size; 4 KiB in single-bank mode. */
#if UMCUB_CFG_FLASH_DUAL_BANK
#define UMCUB_FAMILY_UNIFORM_SECTOR     UMCUB_KB(2)
#else
#define UMCUB_FAMILY_UNIFORM_SECTOR     UMCUB_KB(4)
#endif
#define UMCUB_FAMILY_MIN_SECTOR         UMCUB_FAMILY_UNIFORM_SECTOR
#define UMCUB_FAMILY_BANK2_ADDR         0x08040000u
/* Worst-case page erase time with a margin: DS12288 Table 47 gives 24.47 ms max
 * for a 2 KiB page; none for a 4 KiB page (single bank), taken as twice. */
#define UMCUB_FAMILY_SECTOR_ERASE_MS    (UMCUB_CFG_FLASH_DUAL_BANK ? 40 : 80)
/* The flash programs double words (64 bits, ECC); also the smallest
 * MCUBOOT_BOOT_MAX_ALIGN. */
#define UMCUB_FAMILY_WRITE_ALIGN        8
/* ES0430 §2.2.6: after a system reset the first write to an SRAM instance can
 * be lost - read each one first (SRAM1 in 32 KiB parts, SRAM2, CCM SRAM),
 * before the stack is used (src/boot/startup.c; an application needs the same
 * in its own startup). */
#define UMCUB_FAMILY_RESET_ASM \
    "ldr  r1, =0x20000000          \n" "ldr  r0, [r1]                 \n" \
    "ldr  r1, =0x20008000          \n" "ldr  r0, [r1]                 \n" \
    "ldr  r1, =0x20010000          \n" "ldr  r0, [r1]                 \n" \
    "ldr  r1, =0x20014000          \n" "ldr  r0, [r1]                 \n" \
    "ldr  r1, =0x10000000          \n" "ldr  r0, [r1]                 \n"
/* VTOR: the vector table aligned to its size rounded up to a power of two (G474: 102 IRQs, 118 vectors) */
#define UMCUB_FAMILY_VTOR_ALIGN         0x200

/* Parts the port is verified for, with their SRAM size (the handoff is at
 * its end). G474: SRAM1 80 KiB + SRAM2 16 KiB + CCM SRAM 32 KiB, contiguous
 * from 0x20000000 (CCM SRAM aliased at 0x20018000), DS12288 §3.6. */
#if defined(STM32G474xx)
#define UMCUB_FAMILY_RAM_SIZE           UMCUB_KB(128)
#else
#error "umcub: this STM32G4 part is not verified with the port: check its SRAM size and flash pages (datasheet, reference manual), then add it to the list in port/stm32g4/include/umcub_family_facts.h (docs/porting.md, Adding a part)"
#endif


#endif /* UMCUB_FAMILY_FACTS_H */
