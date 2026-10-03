/*
 * Host emulation of the umcub port for tests: STM32H7-like flash (2 banks,
 * 128 KiB sectors, 32-byte program unit, a programmed word may not be
 * programmed again before an erase), time, reset via longjmp.
 */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fake_port.h"
#include "umcub_port.h"

#define BASE   0x08000000u
#define SIZE   (2u * 1024u * 1024u)
#define SECTOR (128u * 1024u)
#define WORD   32u

uint8_t fake_flash[SIZE];
static uint8_t written[SIZE / WORD];
static uint32_t now_ms;
jmp_buf *fake_reset_jmp;
unsigned fake_flash_erase_count;

void fake_flash_reset(void)
{
    memset(fake_flash, 0xFF, sizeof(fake_flash));
    memset(written, 0, sizeof(written));
}

uint32_t umcub_port_millis(void) { return now_ms++; }
void umcub_port_delay_ms(uint32_t ms) { now_ms += ms; }
void umcub_port_wdg_feed(void) {}
void umcub_port_idle(void) {}
uint32_t umcub_port_irq_save(void) { return 0; }
void umcub_port_irq_restore(uint32_t s) { (void)s; }
uint8_t umcub_port_reset_cause(void) { return UMCUB_RESET_SOFTWARE; }
uint32_t umcub_port_reset_flags(void) { return 0; }

void umcub_port_uid(uint8_t uid[12])
{
    for (int i = 0; i < 12; i++) {
        uid[i] = (uint8_t)(0x10 + i);
    }
}

void umcub_port_reset(void)
{
    if (fake_reset_jmp) {
        longjmp(*fake_reset_jmp, 1);
    }
    fprintf(stderr, "unexpected reset\n");
    exit(2);
}

static int range(uint32_t addr, size_t len)
{
    return addr >= BASE && addr - BASE <= SIZE && len <= SIZE - (addr - BASE);
}

uint32_t umcub_flash_write_align(void) { return WORD; }
uint8_t umcub_flash_erased_val(void) { return 0xFF; }

int umcub_flash_sector_info(uint32_t addr, uint32_t *start, uint32_t *size)
{
    if (!range(addr, 1) || addr == BASE + SIZE) {
        return UMCUB_EINVAL;
    }
    *start = addr & ~(SECTOR - 1u);
    *size = SECTOR;
    return 0;
}

int umcub_flash_read(uint32_t addr, void *dst, size_t len)
{
    if (!range(addr, len)) {
        return UMCUB_EINVAL;
    }
    memcpy(dst, &fake_flash[addr - BASE], len);
    return 0;
}

int umcub_flash_write(uint32_t addr, const void *src, size_t len)
{
    if (!range(addr, len) || addr % WORD || len % WORD) {
        fprintf(stderr, "flash write misaligned 0x%08x %zu\n", addr, len);
        return UMCUB_EINVAL;
    }
    for (size_t o = 0; o < len; o += WORD) {
        uint32_t w = (addr - BASE + (uint32_t)o) / WORD;
        if (written[w]) {
            fprintf(stderr, "ECC violation: word 0x%08x programmed twice\n", addr + (uint32_t)o);
            return UMCUB_EIO;
        }
        written[w] = 1;
        memcpy(&fake_flash[w * WORD], (const uint8_t *)src + o, WORD);
    }
    return 0;
}

int umcub_flash_erase(uint32_t addr, size_t len)
{
    if (!range(addr, len) || addr % SECTOR || len % SECTOR) {
        return UMCUB_EINVAL;
    }
    memset(&fake_flash[addr - BASE], 0xFF, len);
    memset(&written[(addr - BASE) / WORD], 0, len / WORD);
    fake_flash_erase_count += (unsigned)(len / SECTOR);
    return 0;
}

void umcub_port_core2_release(uint32_t vtor) { (void)vtor; }
uint32_t SystemCoreClock = 64000000;

void umcub_handoff_note_transport(uint8_t id) { (void)id; }

uint32_t fake_last_request;
void umcub_handoff_request(uint32_t request, uint32_t arg) { (void)arg; fake_last_request = request; }
