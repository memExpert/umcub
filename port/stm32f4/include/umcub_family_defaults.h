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
/* Slots are usually 128 KiB sectors: erase the whole slot on the first SMP
 * chunk (2 s per sector), see UMCUB_CFG_ERASE_PROGRESSIVELY. */
#ifndef UMCUB_CFG_ERASE_PROGRESSIVELY
#define UMCUB_CFG_ERASE_PROGRESSIVELY   0
#endif
/* Worst-case erase of the largest (128 KiB) sector with x32 parallelism
 * (DS10314 Table 45: 2 s). */
#define UMCUB_FAMILY_SECTOR_ERASE_MS    2000
/* The flash programs 32-bit words (PSIZE x32, 2.7..3.6 V); 8 is the smallest
 * MCUBOOT_BOOT_MAX_ALIGN MCUboot supports. */
#define UMCUB_FAMILY_WRITE_ALIGN        8

/* SRAM size. Only parts checked against their datasheet are listed. */
#if defined(STM32F411xE)
#define UMCUB_FAMILY_RAM_SIZE           UMCUB_KB(128)  /* DS10314: F411xC/xE */
#else
#error "umcub: STM32F4 part not verified yet - add its SRAM size (datasheet) and flash layout (flash.c)"
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
