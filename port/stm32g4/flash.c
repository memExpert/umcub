/*
 * STM32G4 embedded flash driver (RM0440 §3.3). Registers only - LL has no
 * flash API. Also used by the application library (umcub_slot_*).
 *
 *  - program unit: one double word (64 bits + 8 ECC bits); a double word can
 *    be programmed only while it is erased (PROGERR otherwise), so double
 *    words that are all 0xFF are skipped.
 *  - erase unit: one page, 2 KiB in dual-bank mode (DBANK = 1: bank 2 from
 *    the middle of the flash, page numbers per bank, BKER), 4 KiB in
 *    single-bank mode (one bank, 128 pages of 4 KiB on 512 KiB). The size
 *    follows the option bit at run time, umcub_flash_sector_info() reports it
 *    to MCUboot and the slot writer.
 *  - the data cache may hold the old contents of a page that was just
 *    programmed or erased: it is flushed after every operation, so the
 *    plain memory reads below see the new data.
 */
#include <string.h>
#include "g4.h"
#include "umcub_fault.h"

#define WRITE_ALIGN         UMCUB_FAMILY_WRITE_ALIGN
#define KEY1                0x45670123u
#define KEY2                0xCDEF89ABu
#define TIMEOUT_PROGRAM_MS  5u      /* DS12288: 82 us per double word, max */
#define TIMEOUT_ERASE_MS    100u    /* DS12288: 22..40 ms per page */
#define SR_ERRORS           (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
                             FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR | \
                             FLASH_SR_RDERR | FLASH_SR_OPTVERR)

static uint32_t flash_size(void)
{
    return (uint32_t)(*(const volatile uint16_t *)FLASHSIZE_BASE) * 1024u;
}

static bool dual_bank(void)
{
    return (FLASH->OPTR & FLASH_OPTR_DBANK) != 0;
}

static uint32_t page_size(void)
{
    return dual_bank() ? UMCUB_FAMILY_UNIFORM_SECTOR : UMCUB_FAMILY_SINGLE_BANK_PAGE;
}

static bool in_flash(uint32_t addr, size_t len)
{
    uint32_t end = FLASH_BASE + flash_size();
    return addr >= FLASH_BASE && addr <= end && len <= end - addr;
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

/* Drop what the data cache holds of the flash (RM0440 §3.3.3: the cache
 * must be disabled to be reset). */
static void flush_dcache(void)
{
    uint32_t acr = FLASH->ACR;
    if (acr & FLASH_ACR_DCEN) {
        CLEAR_BIT(FLASH->ACR, FLASH_ACR_DCEN);
        SET_BIT(FLASH->ACR, FLASH_ACR_DCRST);
        CLEAR_BIT(FLASH->ACR, FLASH_ACR_DCRST);
        SET_BIT(FLASH->ACR, FLASH_ACR_DCEN);
    }
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

int umcub_flash_sector_info(uint32_t addr, uint32_t *start, uint32_t *size)
{
    if (!in_flash(addr, 1) || addr == FLASH_BASE + flash_size()) {
        return UMCUB_EINVAL;
    }
    uint32_t page = page_size();
    *start = addr & ~(page - 1u);
    *size = page;
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
    int rc = umcub_cm_wait(&FLASH->SR, FLASH_SR_BSY, 0, TIMEOUT_ERASE_MS);

    unlock();
    FLASH->SR = FLASH_SR_EOP | SR_ERRORS;
    while (len && rc == UMCUB_OK) {
        uint32_t lo, hi;
        memcpy(&lo, s, 4);                  /* source may be unaligned */
        memcpy(&hi, s + 4, 4);
        if ((lo & hi) != 0xFFFFFFFFu) {     /* erased already: nothing to do */
            SET_BIT(FLASH->CR, FLASH_CR_PG);
            ((volatile uint32_t *)addr)[0] = lo;
            __ISB();
            ((volatile uint32_t *)addr)[1] = hi;
            UMCUB_FAULT_POINT();        /* test: power cut while the double word is programmed */
            rc = wait_done(TIMEOUT_PROGRAM_MS);
            CLEAR_BIT(FLASH->CR, FLASH_CR_PG);
        }
        addr += 8u;
        s += 8u;
        len -= 8u;
    }
    lock();
    flush_dcache();

    if (rc == UMCUB_OK && memcmp((const void *)start, src, total) != 0) {
        rc = UMCUB_EIO;                 /* e.g. 0xFF.. over a programmed double word */
    }
    return rc;
}

int umcub_flash_erase(uint32_t addr, size_t len)
{
    const uint32_t page = page_size();
    const bool two = dual_bank();
    const uint32_t bank_size = two ? flash_size() / 2u : flash_size();

    if (!in_flash(addr, len) || (addr % page) || (len % page)) {
        return UMCUB_EINVAL;
    }
    int rc = umcub_cm_wait(&FLASH->SR, FLASH_SR_BSY, 0, TIMEOUT_ERASE_MS);

    unlock();
    FLASH->SR = FLASH_SR_EOP | SR_ERRORS;
    while (len && rc == UMCUB_OK) {
        uint32_t off = addr - FLASH_BASE;
        uint32_t bank2 = two && off >= bank_size;
        uint32_t pnb = (bank2 ? off - bank_size : off) / page;
        umcub_port_wdg_feed();
        MODIFY_REG(FLASH->CR, FLASH_CR_PNB | FLASH_CR_BKER,
                   FLASH_CR_PER | (pnb << FLASH_CR_PNB_Pos) | (bank2 ? FLASH_CR_BKER : 0u));
        SET_BIT(FLASH->CR, FLASH_CR_STRT);
        __DSB();
        UMCUB_FAULT_POINT();            /* test: power cut in the middle of a page erase */
        rc = wait_done(TIMEOUT_ERASE_MS);
        CLEAR_BIT(FLASH->CR, FLASH_CR_PER | FLASH_CR_BKER);
        addr += page;
        len -= page;
    }
    lock();
    flush_dcache();
    return rc;
}
