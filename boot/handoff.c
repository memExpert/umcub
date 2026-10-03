/*
 * Handoff area management (bootloader side).
 */
#include <string.h>
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "umcub_handoff.h"
#include "umcub_boot.h"

#define HANDOFF ((volatile umcub_handoff_t *)UMCUB_CFG_SHARED_RAM_ADDR)

/* STM32H7 (and other ECC SRAMs): a write narrower than 32 bits sits in the
 * ECC write buffer until the next RAM access and is LOST on reset. Every
 * store into the handoff area that must survive a reset is a full word. */
static void store_words(volatile uint32_t *dst, const uint32_t *src, size_t words)
{
    while (words--) {
        *dst++ = *src++;
    }
    __asm volatile("dsb" ::: "memory");
}

/* Transport that delivered the last update: written by the SMP mux in
 * recovery mode, carried over the reset into the next handoff info. */
void umcub_handoff_note_transport(uint8_t id)
{
    volatile uint32_t *w = (volatile uint32_t *)((uintptr_t)&HANDOFF->last_transport & ~3u);
    union {
        uint32_t word;
        uint8_t b[4];
    } u = { .word = *w };
    u.b[(uintptr_t)&HANDOFF->last_transport & 3u] = id;
    store_words(w, &u.word, 1);
}

/* Node address handed over by the application; false if none (power-up,
 * application that never set one). */
bool umcub_handoff_node_request(uint16_t *addr)
{
    uint32_t v = HANDOFF->node_addr_req;
    if (HANDOFF->node_magic != UMCUB_NODE_MAGIC || (uint16_t)(v >> 16) != (uint16_t)~v) {
        return false;
    }
    *addr = (uint16_t)v;
    return true;
}

/* Board hook (umcub_board.c): address from DIP switches, EEPROM, ... */
__attribute__((weak)) bool umcub_board_node_address(uint16_t *addr)
{
    (void)addr;
    return false;
}

static uint16_t node_addr;

void umcub_node_init(void)
{
    uint16_t a;
    if (umcub_handoff_node_request(&a) || umcub_board_node_address(&a)) {
        node_addr = a;
    } else {
        node_addr = 0;          /* unassigned: reachable by UID only (see umcub link) */
    }
    /* TODO: address stored in a flash page of its own (not implemented). */
}

uint16_t umcub_node_address(void)
{
    return node_addr;
}

uint8_t umcub_handoff_last_transport(void)
{
    uint8_t id = HANDOFF->last_transport;
    return id <= UMCUB_TRANSPORT_USER ? id : UMCUB_TRANSPORT_NONE;
}

/* Request for the next boot, written by the bootloader itself (text command
 * "boot app" in recovery mode resets with UMCUB_REQ_BOOT_APP). */
void umcub_handoff_request(uint32_t request, uint32_t arg)
{
    const uint32_t r[4] = { UMCUB_REQUEST_MAGIC, request, arg, ~UMCUB_REQUEST_MAGIC ^ request };
    store_words((volatile uint32_t *)HANDOFF, r, 4);
}

uint32_t umcub_handoff_take_request(uint32_t *arg)
{
    uint8_t cause = umcub_port_reset_cause();
    if (cause == UMCUB_RESET_POWER_ON || cause == UMCUB_RESET_BROWNOUT) {
        /* RAM content is undefined (and may carry ECC garbage): initialise. */
        static const uint32_t zero[sizeof(umcub_handoff_t) / 4];
        store_words((volatile uint32_t *)HANDOFF, zero, sizeof(umcub_handoff_t) / 4);
        return UMCUB_REQ_NONE;
    }
    uint32_t req = UMCUB_REQ_NONE;
    if (HANDOFF->request_magic == UMCUB_REQUEST_MAGIC &&
        HANDOFF->request_check == (~UMCUB_REQUEST_MAGIC ^ HANDOFF->request) &&
        (HANDOFF->request == UMCUB_REQ_ENTER_BOOT || HANDOFF->request == UMCUB_REQ_BOOT_APP)) {
        req = HANDOFF->request;
        if (arg) {
            *arg = HANDOFF->request_arg;
        }
    }
    static const uint32_t none[4];
    store_words((volatile uint32_t *)HANDOFF, none, 4);   /* request part */
    return req;
}

void umcub_handoff_publish(const umcub_handoff_t *info)
{
    umcub_handoff_t h;
    memcpy(&h, info, sizeof(h));
    h.magic = UMCUB_HANDOFF_MAGIC;
    h.version = UMCUB_HANDOFF_VERSION;
    h.size = sizeof(umcub_handoff_t);
    h.crc32 = umcub_crc32((const uint8_t *)&h + UMCUB_HANDOFF_INFO_OFFSET, UMCUB_HANDOFF_INFO_LEN);
    /* request and core2 parts are left untouched */
    store_words((volatile uint32_t *)((uintptr_t)HANDOFF + UMCUB_HANDOFF_INFO_OFFSET),
                (const uint32_t *)((const uint8_t *)&h + UMCUB_HANDOFF_INFO_OFFSET),
                (__builtin_offsetof(umcub_handoff_t, core2_magic) - UMCUB_HANDOFF_INFO_OFFSET) / 4);
}
