/*
 * STM32G4 embedded flash driver (RM0440 §3.3). Registers only - LL has no
 * flash API. Also used by the application library (umcub_slot_*).
 *
 *  - program unit: one double word (64 bits + 8 ECC bits); a double word can
 *    be programmed only while it is erased (PROGERR otherwise), so double
 *    words that are all 0xFF are skipped.
 *  - erase unit: one page in the bank mode the configuration sets
 *    (UMCUB_CFG_FLASH_DUAL_BANK, RM0440 Tables 7 and 8): 2 KiB with two banks
 *    (bank 2 from 0x08040000 whatever the flash size, page numbers per bank,
 *    BKER), 4 KiB with one. A device whose option bit DBANK says otherwise
 *    refuses writes and erases: its pages are not where the layout puts them.
 *  - a double ECC error (a double word cut by a power loss) raises an NMI
 *    (RM0440 §3.3.2): umcub_flash_read() reports it as UMCUB_EIO, anywhere
 *    else the bootloader resets.
 *  - the data cache is off during every program / erase and reset after it
 *    (ES0430 §2.2.2, and so the plain memory reads below see the new data).
 */
#include <string.h>
#include "g4.h"
#include "umcub_fault.h"

#define WRITE_ALIGN         UMCUB_FAMILY_WRITE_ALIGN
#define KEY1                0x45670123u
#define KEY2                0xCDEF89ABu
#define TIMEOUT_PROGRAM_MS  5u      /* DS12288 Table 47: 83.35 us per double word, max */
#define TIMEOUT_ERASE_MS    100u    /* DS12288 Table 47: 24.47 ms per 2 KiB page, max (4 KiB: not given) */
#define SR_ERRORS           (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
                             FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR | \
                             FLASH_SR_RDERR | FLASH_SR_OPTVERR)

static uint32_t flash_size(void)
{
    return (uint32_t)(*(const volatile uint16_t *)FLASHSIZE_BASE) * 1024u;
}

#define PAGE                UMCUB_FAMILY_UNIFORM_SECTOR
#define BANK2               UMCUB_FAMILY_BANK2_ADDR

static uint32_t bank_size(void)
{
    return UMCUB_CFG_FLASH_DUAL_BANK ? flash_size() / 2u : flash_size();
}

/* The option bit DBANK matches the configured bank mode. */
static bool mode_ok(void)
{
    return !(FLASH->OPTR & FLASH_OPTR_DBANK) == !UMCUB_CFG_FLASH_DUAL_BANK;
}

/* [addr, addr + len) inside one bank, or inside both where bank 2 follows
 * bank 1 directly (512 KiB parts). */
static bool in_flash(uint32_t addr, size_t len)
{
    uint32_t n = bank_size(), lo = FLASH_BASE, hi = FLASH_BASE + n;
    if (UMCUB_CFG_FLASH_DUAL_BANK) {
        if (addr >= BANK2) {
            lo = BANK2;
            hi = BANK2 + n;
        } else if (hi == BANK2) {
            hi = BANK2 + n;
        }
    }
    return addr >= lo && addr < hi && len <= hi - addr;
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

/* The data cache stays off while a bank is programmed or erased (ES0430
 * §2.2.2: reading one bank while the other is written can corrupt it), and is
 * reset before it goes back on (RM0440 FLASH_ACR: only while it is off), so
 * reads see the new contents. */
static uint32_t dcache_off(void)
{
    uint32_t on = FLASH->ACR & FLASH_ACR_DCEN;
    CLEAR_BIT(FLASH->ACR, FLASH_ACR_DCEN);
    return on;
}

static void dcache_restore(uint32_t on)
{
    SET_BIT(FLASH->ACR, FLASH_ACR_DCRST);
    CLEAR_BIT(FLASH->ACR, FLASH_ACR_DCRST);
    SET_BIT(FLASH->ACR, on);
}

/* BSY is clear: unlock, clear the status and what a timed-out operation left in FLASH_CR. */
static void start_op(void)
{
    unlock();
    FLASH->SR = FLASH_SR_EOP | SR_ERRORS;
    CLEAR_BIT(FLASH->CR, FLASH_CR_PG | FLASH_CR_PER | FLASH_CR_BKER);
}

/* After a timeout BSY may still be set: FLASH_CR stays untouched (locked again by the next operation). */
static void end_op(int rc, uint32_t dcache)
{
    if (rc != UMCUB_ETIMEOUT) {
        lock();
    }
    dcache_restore(dcache);
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
    if (!in_flash(addr, 1)) {
        return UMCUB_EINVAL;
    }
    *start = addr & ~(PAGE - 1u);
    *size = PAGE;
    return UMCUB_OK;
}

#ifdef UMCUB_BUILDING_APP
int umcub_flash_read(uint32_t addr, void *dst, size_t len)
{
    if (!in_flash(addr, len)) {
        return UMCUB_EINVAL;
    }
    memcpy(dst, (const void *)addr, len);     /* the application's own NMI handler sees ECC errors */
    return UMCUB_OK;
}
#else
static volatile uint8_t ecc_read;           /* 1: a read runs, 2: it met a double ECC error */
/* All four flags: one left set - a corrected single error (ECCC) too - keeps the next error, a double one as
 * well, from being flagged and from raising the NMI (RM0440 3.3.2 / FLASH_ECCR: set "only if ECCC/ECCC2/ECCD/
 * ECCD2 are previously cleared"). rc_w1; ECCCIE (CMSIS: ECCIE) stays as it is. ECCC2 / ECCD2 (the upper 64 bits)
 * exist in single-bank mode only, reserved with DBANK = 1. */
#define ECC_FLAGS           (FLASH_ECCR_ECCC | FLASH_ECCR_ECCD | \
                             (UMCUB_CFG_FLASH_DUAL_BANK ? 0u : FLASH_ECCR_ECCC2 | FLASH_ECCR_ECCD2))

static void ecc_clear(void)
{
    FLASH->ECCR = (FLASH->ECCR & FLASH_ECCR_ECCIE) | ECC_FLAGS;
}

void NMI_Handler(void)
{
    uint32_t ecc = FLASH->ECCR & (FLASH_ECCR_ECCD | FLASH_ECCR_ECCD2);
    if (!ecc || !ecc_read) {
        NVIC_SystemReset();                 /* not a guarded read: nothing to return to safely */
    }
    ecc_clear();
    ecc_read = 2;
    __DSB();                                /* ES0430 §2.1.3: no store right before the exception return */
}

int umcub_flash_read(uint32_t addr, void *dst, size_t len)
{
    if (!in_flash(addr, len)) {
        return UMCUB_EINVAL;
    }
    ecc_clear();
    ecc_read = 1;
    memcpy(dst, (const void *)addr, len);
    __DSB();
    int rc = ecc_read == 2 ? UMCUB_EIO : UMCUB_OK;
    ecc_read = 0;
    return rc;
}
#endif

int umcub_flash_write(uint32_t addr, const void *src, size_t len)
{
    if (!in_flash(addr, len) || (addr % WRITE_ALIGN) || (len % WRITE_ALIGN)) {
        return UMCUB_EINVAL;
    }
    if (!mode_ok()) {
        return UMCUB_EIO;
    }
    const uint8_t *s = src;
    const uint32_t start = addr;
    const size_t total = len;
    /* FLASH_CR is not written while BSY is set (RM0440 3.3.5: the bus stalls until it clears): a timeout
     * returns at once and leaves the controller as it is; the next operation clears PG / PER. */
    int rc = umcub_cm_wait(&FLASH->SR, FLASH_SR_BSY, 0, TIMEOUT_ERASE_MS);
    if (rc) {
        return rc;
    }
    uint32_t dcache = dcache_off();
    start_op();
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
            if (rc == UMCUB_ETIMEOUT) {
                break;
            }
            CLEAR_BIT(FLASH->CR, FLASH_CR_PG);
        }
        addr += 8u;
        s += 8u;
        len -= 8u;
    }
    end_op(rc, dcache);

    if (rc == UMCUB_OK && memcmp((const void *)start, src, total) != 0) {
        rc = UMCUB_EIO;                 /* e.g. 0xFF.. over a programmed double word */
    }
    return rc;
}

int umcub_flash_erase(uint32_t addr, size_t len)
{
    if (!in_flash(addr, len) || (addr % PAGE) || (len % PAGE)) {
        return UMCUB_EINVAL;
    }
    if (!mode_ok()) {
        return UMCUB_EIO;
    }
    int rc = umcub_cm_wait(&FLASH->SR, FLASH_SR_BSY, 0, TIMEOUT_ERASE_MS);
    if (rc) {
        return rc;
    }
    uint32_t dcache = dcache_off();
    start_op();
    while (len && rc == UMCUB_OK) {
        bool bank2 = UMCUB_CFG_FLASH_DUAL_BANK && addr >= BANK2;
        uint32_t pnb = (addr - (bank2 ? BANK2 : FLASH_BASE)) / PAGE;
        umcub_port_wdg_feed();
        MODIFY_REG(FLASH->CR, FLASH_CR_PNB | FLASH_CR_BKER,
                   FLASH_CR_PER | (pnb << FLASH_CR_PNB_Pos) | (bank2 ? FLASH_CR_BKER : 0u));
        SET_BIT(FLASH->CR, FLASH_CR_STRT);
        __DSB();
        UMCUB_FAULT_POINT();            /* test: power cut in the middle of a page erase */
        rc = wait_done(TIMEOUT_ERASE_MS);
        if (rc == UMCUB_ETIMEOUT) {
            break;
        }
        CLEAR_BIT(FLASH->CR, FLASH_CR_PER | FLASH_CR_BKER);
        addr += PAGE;
        len -= PAGE;
    }
    end_op(rc, dcache);
    return rc;
}
