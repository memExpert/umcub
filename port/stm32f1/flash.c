/*
 * STM32F1 embedded flash driver (PM0075, RM0008 §3.3). Registers only - LL
 * has no flash API. Also used by the application library (umcub_slot_*).
 *
 *  - program unit: one half-word; a half-word can be programmed only when it
 *    reads 0xFFFF (PGERR otherwise). The port reports 8-byte alignment (the
 *    smallest MCUBOOT_BOOT_MAX_ALIGN), any multiple of 2 would work.
 *  - erase unit: one page, 1 KiB (low/medium density) or 2 KiB.
 *  - bank 1 only (first 512 KiB): XL-density bank 2 is not supported.
 *  - no ECC: reads are plain memory reads.
 */
#include <string.h>
#include "f1.h"
#include "umcub_fault.h"

#define PAGE_SIZE           UMCUB_FAMILY_UNIFORM_SECTOR
#define WRITE_ALIGN         UMCUB_FAMILY_WRITE_ALIGN
#define BANK1_MAX           (512u * 1024u)
#define KEY1                0x45670123u
#define KEY2                0xCDEF89ABu
#define TIMEOUT_PROGRAM_MS  5u      /* datasheet: 52..70 us per half-word */
#define TIMEOUT_ERASE_MS    100u    /* datasheet: 20..40 ms per page */
#define SR_ERRORS           (FLASH_SR_PGERR | FLASH_SR_WRPRTERR)

static uint32_t flash_end(void)
{
    uint32_t kib = *(const volatile uint16_t *)FLASHSIZE_BASE;
    uint32_t size = kib * 1024u;
    return FLASH_BASE + (size > BANK1_MAX ? BANK1_MAX : size);
}

static bool in_flash(uint32_t addr, size_t len)
{
    uint32_t end = flash_end();
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

static int wait_done(uint32_t timeout_ms)
{
    int rc = f1_wait(&FLASH->SR, FLASH_SR_BSY, 0, timeout_ms);
    uint32_t sr = FLASH->SR;
    FLASH->SR = FLASH_SR_EOP | SR_ERRORS;   /* write 1 to clear */
    if (rc != 0) {
        return rc;
    }
    return (sr & SR_ERRORS) ? UMCUB_EIO : UMCUB_OK;
}

uint32_t umcub_flash_write_align(void)
{
    return WRITE_ALIGN;
}

uint8_t umcub_flash_erased_val(void)
{
    return 0xFF;
}

int umcub_flash_sector_info(uint32_t addr, uint32_t *start, uint32_t *size)
{
    if (!in_flash(addr, 1) || addr == flash_end()) {
        return UMCUB_EINVAL;
    }
    *start = addr & ~(PAGE_SIZE - 1u);
    *size = PAGE_SIZE;
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
    int rc = f1_wait(&FLASH->SR, FLASH_SR_BSY, 0, TIMEOUT_ERASE_MS);

    unlock();
    FLASH->SR = FLASH_SR_EOP | SR_ERRORS;
    while (len && rc == UMCUB_OK) {
        uint16_t hw = (uint16_t)(s[0] | (s[1] << 8));   /* source may be unaligned */
        if (hw != 0xFFFFu) {                            /* erased already: nothing to do */
            SET_BIT(FLASH->CR, FLASH_CR_PG);
            *(volatile uint16_t *)addr = hw;
            UMCUB_FAULT_POINT();        /* test: power cut while the half-word is programmed */
            rc = wait_done(TIMEOUT_PROGRAM_MS);
            CLEAR_BIT(FLASH->CR, FLASH_CR_PG);
        }
        addr += 2u;
        s += 2u;
        len -= 2u;
    }
    lock();

    if (rc == UMCUB_OK && memcmp((const void *)start, src, total) != 0) {
        rc = UMCUB_EIO;                 /* e.g. 0xFFFF over a programmed half-word */
    }
    return rc;
}

int umcub_flash_erase(uint32_t addr, size_t len)
{
    if (!in_flash(addr, len) || (addr % PAGE_SIZE) || (len % PAGE_SIZE)) {
        return UMCUB_EINVAL;
    }
    int rc = f1_wait(&FLASH->SR, FLASH_SR_BSY, 0, TIMEOUT_ERASE_MS);

    unlock();
    FLASH->SR = FLASH_SR_EOP | SR_ERRORS;
    while (len && rc == UMCUB_OK) {
        umcub_port_wdg_feed();
        SET_BIT(FLASH->CR, FLASH_CR_PER);
        FLASH->AR = addr;
        SET_BIT(FLASH->CR, FLASH_CR_STRT);
        __DSB();
        UMCUB_FAULT_POINT();            /* test: power cut in the middle of a page erase */
        rc = wait_done(TIMEOUT_ERASE_MS);
        CLEAR_BIT(FLASH->CR, FLASH_CR_PER);
        addr += PAGE_SIZE;
        len -= PAGE_SIZE;
    }
    lock();
    return rc;
}
