/*
 * RAM-resident helper that programs STM32H7 option bytes (RM0399 §4.4.3)
 * - for cases where STM32_Programmer_CLI cannot: version 2.23 maps
 *   STM32H745/H755 (security extension + dual core, configuration 0) to the
 *   single-core option-byte layout and hides BOOT_CM4_ADD0 / BCM4 / BCM7.
 *
 * Loaded and started by tools/h7_ob/h7_ob.sh through GDB. Parameters are
 * patched into `ob_request` before the run; the result lands in `ob_result`.
 */
#include <stdint.h>

#define FLASH_BASE_REG   0x52002000u
#define OPTKEYR          (*(volatile uint32_t *)(FLASH_BASE_REG + 0x08))
#define OPTCR            (*(volatile uint32_t *)(FLASH_BASE_REG + 0x18))
#define OPTSR_CUR        (*(volatile uint32_t *)(FLASH_BASE_REG + 0x1C))
#define OPTSR_PRG        (*(volatile uint32_t *)(FLASH_BASE_REG + 0x20))
#define OPTCCR           (*(volatile uint32_t *)(FLASH_BASE_REG + 0x24))
#define BOOT7_PRG        (*(volatile uint32_t *)(FLASH_BASE_REG + 0x44))
#define BOOT4_PRG        (*(volatile uint32_t *)(FLASH_BASE_REG + 0x4C))
#define BOOT7_CUR        (*(volatile uint32_t *)(FLASH_BASE_REG + 0x40))
#define BOOT4_CUR        (*(volatile uint32_t *)(FLASH_BASE_REG + 0x48))

#define OPTCR_OPTLOCK    (1u << 0)
#define OPTCR_OPTSTART   (1u << 1)
#define OPTSR_OPT_BUSY   (1u << 0)
#define OPTSR_CHANGEERR  (1u << 30)
#define OPTSR_BCM4       (1u << 22)
#define OPTSR_BCM7       (1u << 23)

struct ob_request {
    uint32_t magic;          /* 0x0B0B0B0B */
    uint32_t boot7;          /* new FLASH_BOOT7 value (ADD1 << 16 | ADD0) or 0xFFFFFFFF = keep */
    uint32_t boot4;          /* new FLASH_BOOT4 value or 0xFFFFFFFF = keep */
    uint32_t bcm;            /* bit0 BCM7, bit1 BCM4, 0xFFFFFFFF = keep */
};

struct ob_result {
    uint32_t status;         /* 1 ok, 0x8000xxxx error */
    uint32_t optsr_cur;
    uint32_t boot7_cur;
    uint32_t boot4_cur;
};

volatile struct ob_request ob_request = { 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
volatile struct ob_result ob_result;

static __attribute__((noreturn)) void done(uint32_t status)
{
    ob_result.status = status;
    ob_result.optsr_cur = OPTSR_CUR;
    ob_result.boot7_cur = BOOT7_CUR;
    ob_result.boot4_cur = BOOT4_CUR;
    for (;;) {
        __asm volatile("bkpt #0");
    }
}

__attribute__((noreturn, used, section(".text.entry"))) void ob_entry(void)
{
    if (ob_request.magic != 0x0B0B0B0Bu) {
        done(0x80000001u);
    }
    if (OPTCR & OPTCR_OPTLOCK) {
        OPTKEYR = 0x08192A3Bu;
        OPTKEYR = 0x4C5D6E7Fu;
    }
    if (OPTCR & OPTCR_OPTLOCK) {
        done(0x80000002u);           /* unlock refused */
    }
    OPTCCR = OPTSR_CHANGEERR;        /* clear OPTCHANGEERR (CLR_OPTCHANGEERR, bit 30) */

    if (ob_request.boot7 != 0xFFFFFFFFu) {
        BOOT7_PRG = ob_request.boot7;
    }
    if (ob_request.boot4 != 0xFFFFFFFFu) {
        BOOT4_PRG = ob_request.boot4;
    }
    if (ob_request.bcm != 0xFFFFFFFFu) {
        uint32_t v = OPTSR_PRG & ~(OPTSR_BCM4 | OPTSR_BCM7);
        v |= (ob_request.bcm & 1u) ? OPTSR_BCM7 : 0;
        v |= (ob_request.bcm & 2u) ? OPTSR_BCM4 : 0;
        OPTSR_PRG = v;
    }
    OPTCR |= OPTCR_OPTSTART;
    for (uint32_t n = 0; OPTSR_CUR & OPTSR_OPT_BUSY; n++) {
        if (n > 200000000u) {
            OPTCR |= OPTCR_OPTLOCK;
            done(0x80000003u);       /* timeout */
        }
    }
    uint32_t err = OPTSR_CUR & OPTSR_CHANGEERR;
    OPTCR |= OPTCR_OPTLOCK;
    done(err ? 0x80000004u : 1u);
}
