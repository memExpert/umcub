/*
 * Peripherals that work the same on every STM32 family, shared by the family
 * ports (header only).
 */
#ifndef UMCUB_STM32_COMMON_H
#define UMCUB_STM32_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "umcub_family_cmsis.h"
#include "umcub_port.h"
#include "../cortexm/cortexm_port.h"

/* IWDG with prescaler /256: `timeout_ms` in LSI/256 counts (1..4095), the
 * family passes its LSI frequency (the real timeout follows the LSI spread). */
static inline void stm32_iwdg_start(IWDG_TypeDef *iwdg, uint32_t lsi_hz, uint32_t timeout_ms)
{
    uint32_t reload = timeout_ms * (lsi_hz / 1000u) / 256u;
    if (reload == 0) {
        reload = 1;
    } else if (reload > 0xFFFu) {
        reload = 0xFFFu;
    }
    iwdg->KR = 0xCCCC;
    iwdg->KR = 0x5555;
    iwdg->PR = 6;
    iwdg->RLR = reload;
    (void)umcub_cm_wait(&iwdg->SR, IWDG_SR_PVU | IWDG_SR_RVU, 0, 100);
    iwdg->KR = 0xAAAA;
}

/* Always reloaded: a hardware watchdog (option byte) runs without
 * UMCUB_CFG_WATCHDOG_MS too, and reloading a stopped IWDG is a no-op. */
static inline void stm32_iwdg_feed(IWDG_TypeDef *iwdg)
{
    iwdg->KR = 0xAAAA;
}

/* UART ring between the interrupt and the main loop: receive (filled by
 * the interrupt) or transmit (UMCUB_CFG_UART_TX_RING, emptied by it).
 * `size` is a power of two; head and tail run freely. */
typedef struct {
    volatile uint32_t head;
    volatile uint32_t tail;
} stm32_ring_t;

/* false = full, `c` not stored */
static inline bool stm32_ring_put(stm32_ring_t *r, uint8_t *buf, uint32_t size, uint8_t c)
{
    uint32_t h = r->head;
    if ((uint32_t)(h - r->tail) >= size) {
        return false;
    }
    buf[h & (size - 1u)] = c;
    __DMB();                    /* data before index */
    r->head = h + 1u;
    return true;
}

/* false = empty */
static inline bool stm32_ring_pop(stm32_ring_t *r, const uint8_t *buf, uint32_t size, uint8_t *c)
{
    uint32_t t = r->tail;
    if (t == r->head) {
        return false;
    }
    *c = buf[t & (size - 1u)];
    r->tail = t + 1u;
    return true;
}

/* Transmit ring: queue `len` bytes and let the TX-empty interrupt (`txeie` in
 * `cr1`) send them; waits up to 100 ms for room, then drops the rest (stuck). */
static inline void stm32_ring_write(stm32_ring_t *r, uint8_t *buf, uint32_t size, const uint8_t *p, size_t len,
                                    volatile uint32_t *cr1, uint32_t txeie)
{
    while (len--) {
        uint32_t start = umcub_port_millis();
        while (!stm32_ring_put(r, buf, size, *p)) {
            if ((uint32_t)(umcub_port_millis() - start) > 100u) {
                return;
            }
        }
        p++;
        *cr1 |= txeie;
    }
}

/* Wait until the interrupt has taken every queued byte (or `timeout_ms`). */
static inline void stm32_ring_drain(const stm32_ring_t *r, uint32_t timeout_ms)
{
    uint32_t start = umcub_port_millis();
    while (r->tail != r->head && (uint32_t)(umcub_port_millis() - start) < timeout_ms) {
    }
}

static inline size_t stm32_ring_get(stm32_ring_t *r, const uint8_t *buf, uint32_t size, uint8_t *out, size_t max)
{
    size_t n = 0;
    uint32_t head = r->head;
    __DMB();
    while (n < max && r->tail != head) {
        out[n++] = buf[r->tail & (size - 1u)];
        r->tail++;
    }
    return n;
}

#endif
