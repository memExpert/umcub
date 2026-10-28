/*
 * STM32F4 family defaults (applied after the board config, before the
 * generic defaults).
 */
#ifndef UMCUB_FAMILY_DEFAULTS_H
#define UMCUB_FAMILY_DEFAULTS_H

#define UMCUB_FAMILY_STM32F4            1

/* Sectors of different sizes (RM0383 Table 4: 4 x 16, 64, then 128 KiB):
 * no UMCUB_FAMILY_UNIFORM_SECTOR, the driver reports each sector. */
#define UMCUB_FAMILY_MIN_SECTOR         UMCUB_KB(16)
#define UMCUB_FAMILY_SECTOR_AT(a)       ((a) < 0x08010000 ? UMCUB_KB(16) : (a) < 0x08020000 ? UMCUB_KB(64) : UMCUB_KB(128))
/* Image header = vector table alignment of the application: Cortex-M4 VTOR
 * needs 128 words for up to 128 vectors (PM0214 §4.4.4; the F411 has 102). */
#ifndef UMCUB_CFG_IMAGE_HEADER_SIZE
#define UMCUB_CFG_IMAGE_HEADER_SIZE     0x200
#endif
/* Encrypted images: no in-place decryption (it buffers one sector in RAM -
 * 128 KiB); encrypted updates go through the secondary slot. */
#ifndef UMCUB_CFG_ENC_INPLACE
#define UMCUB_CFG_ENC_INPLACE           0
#endif
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
#error "umcub: this STM32F4 part is not verified with the port: check its SRAM size and flash sectors (datasheet, reference manual), then add it to the list in port/stm32f4/include/umcub_family_defaults.h (docs/porting.md, Adding a part)"
#endif

#ifndef UMCUB_CFG_BOOT_ADDR
#define UMCUB_CFG_BOOT_ADDR             0x08000000
#endif
/* Last 256 bytes of SRAM (kept over reset; the application must not use them). */
#ifndef UMCUB_CFG_SHARED_RAM_ADDR
#define UMCUB_CFG_SHARED_RAM_ADDR       (0x20000000 + UMCUB_FAMILY_RAM_SIZE - 256)
#endif
/* TEST ONLY (power-loss emulation): the last 16 bytes of the handoff area,
 * behind umcub_handoff_t - kept over reset, used by nobody else. */
#ifndef UMCUB_CFG_TEST_FAULT_ADDR
#define UMCUB_CFG_TEST_FAULT_ADDR       (UMCUB_CFG_SHARED_RAM_ADDR + 240)
#endif

#endif /* UMCUB_FAMILY_DEFAULTS_H */
