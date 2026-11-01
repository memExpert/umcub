/*
 * STM32F4 family facts (behind the generated umcub_config.h). Option defaults: port/Kconfig.
 */
#ifndef UMCUB_FAMILY_FACTS_H
#define UMCUB_FAMILY_FACTS_H

#define UMCUB_FAMILY_STM32F4            1

/* Sectors of different sizes (RM0383 Table 4: 4 x 16, 64, then 128 KiB):
 * no UMCUB_FAMILY_UNIFORM_SECTOR, the driver reports each sector. */
#define UMCUB_FAMILY_MIN_SECTOR         UMCUB_KB(16)
#define UMCUB_FAMILY_SECTOR_AT(a)       ((a) < 0x08010000 ? UMCUB_KB(16) : (a) < 0x08020000 ? UMCUB_KB(64) : UMCUB_KB(128))
/* Worst-case erase of the largest (128 KiB) sector with x32 parallelism
 * (DS10314 Table 45: 2 s). */
#define UMCUB_FAMILY_SECTOR_ERASE_MS    2000
/* The flash programs 32-bit words (PSIZE x32, 2.7..3.6 V); 8 is the smallest
 * MCUBOOT_BOOT_MAX_ALIGN MCUboot supports. */
#define UMCUB_FAMILY_WRITE_ALIGN        8

/* Parts the port is verified for, with their SRAM size (the handoff is at
 * its end); the sector map above holds up to 1 MiB (not the dual-bank
 * F42x/43x). */
#if defined(STM32F411xE)
#define UMCUB_FAMILY_RAM_SIZE           UMCUB_KB(128)  /* DS10314: F411xC/xE */
#else
#error "umcub: this STM32F4 part is not verified with the port: check its SRAM size and flash sectors (datasheet, reference manual), then add it to the list in port/stm32f4/include/umcub_family_facts.h (docs/porting.md, Adding a part)"
#endif


#endif /* UMCUB_FAMILY_FACTS_H */
