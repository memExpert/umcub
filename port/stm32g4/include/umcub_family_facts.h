/*
 * STM32G4 family facts (behind the generated umcub_config.h). Option defaults: port/Kconfig.
 */
#ifndef UMCUB_FAMILY_FACTS_H
#define UMCUB_FAMILY_FACTS_H

#define UMCUB_FAMILY_STM32G4            1

/* Flash page (erase unit), RM0440 §3.3.1: 2 KiB in dual-bank mode (option
 * bit DBANK = 1, the factory setting on the category 3 parts), 4 KiB in
 * single-bank mode. The layout is checked for 2 KiB pages; flash.c reports
 * the page size the option bit gives at run time. A layout whose bootloader
 * and slot boundaries are also 4 KiB aligned works in both modes; one that is
 * only 2 KiB aligned needs DBANK = 1. */
#define UMCUB_FAMILY_UNIFORM_SECTOR     UMCUB_KB(2)
#define UMCUB_FAMILY_MIN_SECTOR         UMCUB_FAMILY_UNIFORM_SECTOR
#define UMCUB_FAMILY_SINGLE_BANK_PAGE   UMCUB_KB(4)
/* Worst-case page erase time (DS12288 Table 54: tERASE 22 ms typical, 40 ms max). */
#define UMCUB_FAMILY_SECTOR_ERASE_MS    40
/* The flash programs double words (64 bits, ECC); also the smallest
 * MCUBOOT_BOOT_MAX_ALIGN. */
#define UMCUB_FAMILY_WRITE_ALIGN        8

/* Parts the port is verified for, with their SRAM size (the handoff is at
 * its end). G474: SRAM1 80 KiB + SRAM2 16 KiB + CCM SRAM 32 KiB, contiguous
 * from 0x20000000 (CCM SRAM aliased at 0x20018000), DS12288 §3.6. */
#if defined(STM32G474xx)
#define UMCUB_FAMILY_RAM_SIZE           UMCUB_KB(128)
#else
#error "umcub: this STM32G4 part is not verified with the port: check its SRAM size and flash pages (datasheet, reference manual), then add it to the list in port/stm32g4/include/umcub_family_facts.h (docs/porting.md, Adding a part)"
#endif


#endif /* UMCUB_FAMILY_FACTS_H */
