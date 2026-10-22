/*
 * STM32F4 embedded flash driver (RM0383 §3). Registers only - LL has no
 * flash API. Also used by the application library (umcub_slot_*).
 *
 *  - program unit: one 32-bit word (PSIZE x32, supply 2.7..3.6 V, RM0383
 *    Table 6). The port reports 8-byte alignment (the smallest
 *    MCUBOOT_BOOT_MAX_ALIGN).
 *  - erase unit: one sector, 4 x 16 KiB, 64 KiB, then 128 KiB (RM0383
 *    Table 4; the same first 1 MiB on every single-bank F4).
 *  - single bank, at most 1 MiB: the dual-bank F42x/F43x layout is not supported.
 *  - no ECC: reads are plain memory reads.
 */
#include <string.h>
#include "f4.h"
#include "umcub_fault.h"

#define WRITE_ALIGN         UMCUB_FAMILY_WRITE_ALIGN
#define FLASH_MAX           (1024u * 1024u)
#define KEY1                0x45670123u
#define KEY2                0xCDEF89ABu
#define TIMEOUT_PROGRAM_MS  5u      /* DS10314 Table 45: word 100 us max */
#define SR_ERRORS           (FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | \
                             FLASH_SR_PGSERR | FLASH_SR_RDERR)

static uint32_t flash_end(void)
{
    uint32_t kib = *(const volatile uint16_t *)FLASHSIZE_BASE;
    uint32_t size = kib * 1024u;
    return FLASH_BASE + (size > FLASH_MAX ? FLASH_MAX : size);
}

static bool in_flash(uint32_t addr, size_t len)
{
    uint32_t end = flash_end();
    return addr >= FLASH_BASE && addr <= end && len <= end - addr;
}

/* Sector number, start and size of the sector holding `addr` (in flash). The
 * map is UMCUB_FAMILY_SECTOR_AT(); every sector is aligned to its size. */
static unsigned sector_of(uint32_t addr, uint32_t *start, uint32_t *size)
{
    *size = UMCUB_FAMILY_SECTOR_AT(addr);
    *start = addr & ~(*size - 1u);
    uint32_t off = *start - FLASH_BASE;
    return *size == UMCUB_KB(16) ? off / UMCUB_KB(16) : *size == UMCUB_KB(64) ? 4u : 4u + off / UMCUB_KB(128);
}

static void unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = KEY1;
        FLASH->KEYR = KEY2;
    }
}

static void lock(void)
{
    SET_BIT(FLASH->CR, FLASH_CR_LOCK);
}

static int wait_done(uint32_t timeout_ms)
{
    int rc = umcub_cm_wait(&FLASH->SR, FLASH_SR_BSY, 0, timeout_ms);
    uint32_t sr = FLASH->SR;
    FLASH->SR = FLASH_SR_EOP | SR_ERRORS;   /* write 1 to clear */
    if (rc != 0) {
        return rc;
    }
    return (sr & SR_ERRORS) ? UMCUB_EIO : UMCUB_OK;
}

/* DS10314 Table 45 (x32): 16 KiB 500 ms, 64 KiB 1.1 s, 128 KiB 2 s max. */
static uint32_t erase_timeout_ms(uint32_t size)
{
    return size <= 0x4000u ? 600u : size <= 0x10000u ? 1300u : 2400u;
}

int umcub_flash_sector_info(uint32_t addr, uint32_t *start, uint32_t *size)
{
    if (!in_flash(addr, 1) || addr == flash_end()) {
        return UMCUB_EINVAL;
    }
    (void)sector_of(addr, start, size);
    return UMCUB_OK;
}

int umcub_flash_read(uint32_t addr, void *dst, size_t len)
{
    if (!in_flash(addr, len)) {
        return UMCUB_EINVAL;
    }
    memcpy(dst, (const void *)addr, len);
    return UMCUB_OK;
}

int umcub_flash_write(uint32_t addr, const void *src, size_t len)
{
    if (!in_flash(addr, len) || (addr % WRITE_ALIGN) || (len % WRITE_ALIGN)) {
        return UMCUB_EINVAL;
    }
    const uint8_t *s = src;
    const uint32_t start = addr;
    const size_t total = len;
    int rc = umcub_cm_wait(&FLASH->SR, FLASH_SR_BSY, 0, erase_timeout_ms(0x20000u));

    unlock();
    FLASH->SR = FLASH_SR_EOP | SR_ERRORS;
    MODIFY_REG(FLASH->CR, FLASH_CR_PSIZE | FLASH_CR_SER | FLASH_CR_SNB, FLASH_CR_PSIZE_1);
    while (len && rc == UMCUB_OK) {
        uint32_t w;
        memcpy(&w, s, 4);                   /* source may be unaligned */
        if (w != 0xFFFFFFFFu) {             /* erased already: nothing to do */
            SET_BIT(FLASH->CR, FLASH_CR_PG);
            *(volatile uint32_t *)addr = w;
            UMCUB_FAULT_POINT();            /* test: power cut while the word is programmed */
            rc = wait_done(TIMEOUT_PROGRAM_MS);
            CLEAR_BIT(FLASH->CR, FLASH_CR_PG);
        }
        addr += 4u;
        s += 4u;
        len -= 4u;
    }
    lock();

    if (rc == UMCUB_OK && memcmp((const void *)start, src, total) != 0) {
        rc = UMCUB_EIO;                     /* e.g. a bit that would have to go from 0 to 1 */
    }
    return rc;
}

int umcub_flash_erase(uint32_t addr, size_t len)
{
    uint32_t start, size;
    if (!in_flash(addr, len)) {
        return UMCUB_EINVAL;
    }
    /* whole sectors only */
    for (uint32_t a = addr, end = addr + len; a < end; a = start + size) {
        (void)sector_of(a, &start, &size);
        if (start != a || size > end - a) {
            return UMCUB_EINVAL;
        }
    }
    int rc = umcub_cm_wait(&FLASH->SR, FLASH_SR_BSY, 0, erase_timeout_ms(0x20000u));

    unlock();
    FLASH->SR = FLASH_SR_EOP | SR_ERRORS;
    while (len && rc == UMCUB_OK) {
        umcub_port_wdg_feed();
        unsigned n = sector_of(addr, &start, &size);
        MODIFY_REG(FLASH->CR, FLASH_CR_PSIZE | FLASH_CR_SNB | FLASH_CR_PG,
                   FLASH_CR_PSIZE_1 | FLASH_CR_SER | n << FLASH_CR_SNB_Pos);
        SET_BIT(FLASH->CR, FLASH_CR_STRT);
        __DSB();
        UMCUB_FAULT_POINT();                /* test: power cut in the middle of a sector erase */
        rc = wait_done(erase_timeout_ms(size));
        CLEAR_BIT(FLASH->CR, FLASH_CR_SER | FLASH_CR_SNB);
        addr += size;
        len -= size;
    }
    lock();
    /* Erase does not update the I/D caches (programming does): flush them,
     * with the caches off (RM0383 §3.5.4, FLASH_ACR). */
    uint32_t acr = FLASH->ACR;
    FLASH->ACR = acr & ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN);
    FLASH->ACR = (acr & ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN)) | FLASH_ACR_ICRST | FLASH_ACR_DCRST;
    FLASH->ACR = acr;
    return rc;
}
