/*
 * STM32F1 family facts (behind the generated umcub_config.h). Option defaults: port/Kconfig.
 */
#ifndef UMCUB_FAMILY_FACTS_H
#define UMCUB_FAMILY_FACTS_H

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

/* Parts the port is verified for, with their SRAM size (the handoff is at
 * its end; sizes differ inside the F101/F102 lines). */
#if defined(STM32F103xB)
#define UMCUB_FAMILY_RAM_SIZE           UMCUB_KB(20)   /* DS5319: F103x8/xB */
#else
#error "umcub: this STM32F1 part is not verified with the port: check its SRAM size and flash pages (datasheet, reference manual), then add it to the list in port/stm32f1/include/umcub_family_facts.h (docs/porting.md, Adding a part)"
#endif

/* bxCAN: classic CAN only; on F101/F102/F103 it shares 512 bytes of SRAM
 * with the USB peripheral (RM0008 §24.1): not both at the same time. */
#define UMCUB_FAMILY_CAN_NO_FD          1
#define UMCUB_FAMILY_CAN_USB_SHARED_RAM 1


#endif /* UMCUB_FAMILY_FACTS_H */
