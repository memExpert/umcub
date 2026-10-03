/*
 * STM32H7 embedded flash driver (RM0399 §4). Registers only - LL has no
 * flash API. Also used by the application library (umcub_slot_*).
 *
 *  - program unit: one 256-bit flash word (32 bytes); a flash word can be
 *    programmed only once between erases (ECC).
 *  - sector erase: 128 KiB, per bank control registers (CR1/CR2).
 *  - reads are plain memory reads; on a double ECC error the bus faults, so
 *    reads of possibly half-programmed words go through umcub_flash_read()
 *    which checks DBECCERR afterwards.
 */
#include <string.h>
#include "h7.h"
#include "umcub_fault.h"

#define FLASH_WORD          32u
#define SECTOR_SIZE         FLASH_SECTOR_SIZE
#define BANK_SIZE           (FLASH_SIZE / 2u)
#define FLASH_END_ADDR      (FLASH_BANK1_BASE + FLASH_SIZE)
#define KEY1                0x45670123u
#define KEY2                0xCDEF89ABu
#define TIMEOUT_PROGRAM_MS  50u
#define TIMEOUT_ERASE_MS    4000u   /* datasheet max 4 s per 128 KiB sector at x8 */

#define SR_ERRORS   (FLASH_SR_WRPERR | FLASH_SR_PGSERR | FLASH_SR_STRBERR | FLASH_SR_INCERR | \
                     FLASH_SR_OPERR | FLASH_SR_RDPERR | FLASH_SR_RDSERR | FLASH_SR_SNECCERR | \
                     FLASH_SR_DBECCERR | FLASH_SR_CRCRDERR)
#define CCR_ALL     (FLASH_CCR_CLR_EOP | FLASH_CCR_CLR_WRPERR | FLASH_CCR_CLR_PGSERR | \
                     FLASH_CCR_CLR_STRBERR | FLASH_CCR_CLR_INCERR | FLASH_CCR_CLR_OPERR | \
                     FLASH_CCR_CLR_RDPERR | FLASH_CCR_CLR_RDSERR | FLASH_CCR_CLR_SNECCERR | \
                     FLASH_CCR_CLR_DBECCERR | FLASH_CCR_CLR_CRCEND | FLASH_CCR_CLR_CRCRDERR)

struct bank {
    volatile uint32_t *keyr;
    volatile uint32_t *cr;
    volatile uint32_t *sr;
    volatile uint32_t *ccr;
};

static const struct bank banks[2] = {
    { &FLASH->KEYR1, &FLASH->CR1, &FLASH->SR1, &FLASH->CCR1 },
#if defined(DUAL_BANK)
    { &FLASH->KEYR2, &FLASH->CR2, &FLASH->SR2, &FLASH->CCR2 },
#else
    { &FLASH->KEYR1, &FLASH->CR1, &FLASH->SR1, &FLASH->CCR1 },
#endif
};

static const struct bank *bank_of(uint32_t addr)
{
#if defined(DUAL_BANK)
    return &banks[(addr - FLASH_BANK1_BASE) >= BANK_SIZE ? 1 : 0];
#else
    (void)addr;
    return &banks[0];
#endif
}

static bool in_flash(uint32_t addr, size_t len)
{
    return addr >= FLASH_BANK1_BASE && addr <= FLASH_END_ADDR && len <= FLASH_END_ADDR - addr;
}

/* The application library may run with the CM7 D-cache on: drop cached
 * copies of flash that was just programmed/erased or is about to be read. */
static void dcache_invalidate(uint32_t addr, size_t len)
{
#if !H7_IS_CM4
    if (SCB->CCR & SCB_CCR_DC_Msk) {
        uint32_t start = addr & ~31u;
        SCB_InvalidateDCache_by_Addr((void *)start, (int32_t)(len + (addr - start)));
    }
#else
    (void)addr;
    (void)len;
#endif
}

static void unlock(const struct bank *b)
{
    if (*b->cr & FLASH_CR_LOCK) {
        *b->keyr = KEY1;
        *b->keyr = KEY2;
    }
}

static void lock(const struct bank *b)
{
    SET_BIT(*b->cr, FLASH_CR_LOCK);
}

static int wait_done(const struct bank *b, uint32_t timeout_ms)
{
    int rc = h7_wait(b->sr, FLASH_SR_QW, 0, timeout_ms);
    uint32_t sr = *b->sr;
    *b->ccr = CCR_ALL;
    if (rc != 0) {
        return rc;
    }
    return (sr & (SR_ERRORS & ~FLASH_SR_SNECCERR)) ? UMCUB_EIO : UMCUB_OK;
}

uint32_t umcub_flash_write_align(void)
{
    return FLASH_WORD;
}

uint8_t umcub_flash_erased_val(void)
{
    return 0xFF;
}

int umcub_flash_sector_info(uint32_t addr, uint32_t *start, uint32_t *size)
{
    if (!in_flash(addr, 1) || addr == FLASH_END_ADDR) {
        return UMCUB_EINVAL;
    }
    *start = addr & ~(SECTOR_SIZE - 1u);
    *size = SECTOR_SIZE;
    return UMCUB_OK;
}

int umcub_flash_read(uint32_t addr, void *dst, size_t len)
{
    if (!in_flash(addr, len)) {
        return UMCUB_EINVAL;
    }
    /* A word interrupted by power loss while programming can carry a double
     * ECC error and fault the load. Read with FAULTMASK + BFHFNMIGN so the
     * bus error is ignored, then report it from the status register. */
    banks[0].ccr[0] = FLASH_CCR_CLR_DBECCERR | FLASH_CCR_CLR_SNECCERR;
    banks[1].ccr[0] = FLASH_CCR_CLR_DBECCERR | FLASH_CCR_CLR_SNECCERR;
    dcache_invalidate(addr, len);
    uint32_t faultmask = __get_FAULTMASK();
    __set_FAULTMASK(1);
    SCB->CCR |= SCB_CCR_BFHFNMIGN_Msk;
    __DSB();
    __ISB();
    memcpy(dst, (const void *)addr, len);
    __DSB();
    SCB->CCR &= ~SCB_CCR_BFHFNMIGN_Msk;
    __set_FAULTMASK(faultmask);
    __ISB();
    if ((*banks[0].sr | *banks[1].sr) & FLASH_SR_DBECCERR) {
        banks[0].ccr[0] = FLASH_CCR_CLR_DBECCERR;
        banks[1].ccr[0] = FLASH_CCR_CLR_DBECCERR;
        return UMCUB_EIO;
    }
    return UMCUB_OK;
}

int umcub_flash_write(uint32_t addr, const void *src, size_t len)
{
    if (!in_flash(addr, len) || (addr % FLASH_WORD) || (len % FLASH_WORD)) {
        return UMCUB_EINVAL;
    }
    const uint8_t *s = src;
    int rc = UMCUB_OK;

    while (len && rc == UMCUB_OK) {
        const struct bank *b = bank_of(addr);
        uint32_t word[FLASH_WORD / 4];
        memcpy(word, s, FLASH_WORD);   /* source may be unaligned */

        unlock(b);
        *b->ccr = CCR_ALL;
        /* PSIZE x64 is valid for VOS0..3 at VDD >= 1.7 V (RM0399 §4.3.9). */
        MODIFY_REG(*b->cr, FLASH_CR_PSIZE | FLASH_CR_SER | FLASH_CR_BER,
                   FLASH_CR_PSIZE_1 | FLASH_CR_PSIZE_0 | FLASH_CR_PG);
        __ISB();
        __DSB();

        volatile uint32_t *d = (volatile uint32_t *)addr;
        uint32_t irq = umcub_port_irq_save();
        for (unsigned i = 0; i < FLASH_WORD / 4; i++) {
            d[i] = word[i];
        }
        __ISB();
        __DSB();
        umcub_port_irq_restore(irq);
        UMCUB_FAULT_POINT();            /* test: power cut while the word is programmed */

        rc = wait_done(b, TIMEOUT_PROGRAM_MS);
        CLEAR_BIT(*b->cr, FLASH_CR_PG);
        lock(b);
        dcache_invalidate(addr, FLASH_WORD);

        if (rc == UMCUB_OK && memcmp((const void *)addr, word, FLASH_WORD) != 0) {
            rc = UMCUB_EIO;
        }
        addr += FLASH_WORD;
        s += FLASH_WORD;
        len -= FLASH_WORD;
    }
    return rc;
}

int umcub_flash_erase(uint32_t addr, size_t len)
{
    if (!in_flash(addr, len) || (addr % SECTOR_SIZE) || (len % SECTOR_SIZE)) {
        return UMCUB_EINVAL;
    }
    int rc = UMCUB_OK;

    while (len && rc == UMCUB_OK) {
        const struct bank *b = bank_of(addr);
        uint32_t snb = ((addr - FLASH_BANK1_BASE) % BANK_SIZE) / SECTOR_SIZE;

        umcub_port_wdg_feed();
        unlock(b);
        *b->ccr = CCR_ALL;
        MODIFY_REG(*b->cr, FLASH_CR_PSIZE | FLASH_CR_SNB | FLASH_CR_PG | FLASH_CR_BER,
                   FLASH_CR_PSIZE_1 | FLASH_CR_PSIZE_0 | FLASH_CR_SER | (snb << FLASH_CR_SNB_Pos));
        SET_BIT(*b->cr, FLASH_CR_START);
        __DSB();
        UMCUB_FAULT_POINT();            /* test: power cut in the middle of a sector erase */

        rc = wait_done(b, TIMEOUT_ERASE_MS);
        CLEAR_BIT(*b->cr, FLASH_CR_SER | FLASH_CR_SNB);
        lock(b);
        dcache_invalidate(addr, SECTOR_SIZE);

        addr += SECTOR_SIZE;
        len -= SECTOR_SIZE;
    }
#if !H7_IS_CM4
    /* Code may be executed from the erased/programmed area later. */
    if (SCB->CCR & SCB_CCR_IC_Msk) {
        SCB_InvalidateICache();
    }
#endif
    return rc;
}
