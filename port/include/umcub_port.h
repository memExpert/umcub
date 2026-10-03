/*
 * umcub hardware port API - the only contract between portable code
 * (boot core, MCUboot glue, transports, application library) and an MCU
 * family port (port/<family>/).
 *
 * Return codes: 0 on success, negative UMCUB_E* on failure.
 */
#ifndef UMCUB_PORT_H
#define UMCUB_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UMCUB_OK         0
#define UMCUB_EINVAL    -1   /* bad argument / alignment */
#define UMCUB_EIO       -2   /* hardware reported an error */
#define UMCUB_ETIMEOUT  -3
#define UMCUB_ENOTSUP   -4
#define UMCUB_EBUSY     -5

/* ------------------------------------------------------------------------ */
/* System                                                                   */
/* ------------------------------------------------------------------------ */

/* Power, clocks, SysTick (1 ms), caches as the bootloader needs them. */
void umcub_port_init(void);

/* Undo everything umcub_port_init() and the drivers did: peripherals back in
 * reset, clock tree on the reset-default oscillator with PLLs off, SysTick
 * stopped, all NVIC interrupts disabled and cleared, caches as after reset. */
void umcub_port_deinit(void);

uint32_t umcub_port_millis(void);
void umcub_port_delay_ms(uint32_t ms);

void umcub_port_wdg_start(uint32_t timeout_ms);
void umcub_port_wdg_feed(void);

__attribute__((noreturn)) void umcub_port_reset(void);

/* Load MSP/VTOR from the vector table at `vtor` and branch to its reset
 * handler. Caller has already called umcub_port_deinit(). */
__attribute__((noreturn)) void umcub_port_jump(uint32_t vtor);

/* 96-bit unique device id (12 bytes). */
void umcub_port_uid(uint8_t uid[12]);

/* Why the MCU came out of reset (UMCUB_RESET_*), sampled at init. */
#define UMCUB_RESET_UNKNOWN   0
#define UMCUB_RESET_POWER_ON  1
#define UMCUB_RESET_PIN       2
#define UMCUB_RESET_SOFTWARE  3
#define UMCUB_RESET_WATCHDOG  4
#define UMCUB_RESET_BROWNOUT  5
#define UMCUB_RESET_OTHER     6
uint8_t umcub_port_reset_cause(void);
/* Raw reset flags (family specific), latched and cleared at init. */
uint32_t umcub_port_reset_flags(void);

/* Global interrupt enable/disable (nesting not supported, not needed). */
uint32_t umcub_port_irq_save(void);
void umcub_port_irq_restore(uint32_t state);

/* Polling hook called from every busy loop of the bootloader. */
void umcub_port_idle(void);

/* ------------------------------------------------------------------------ */
/* GPIO (entry pin, board LEDs). Pin = UMCUB_PIN(port, pin, af).             */
/* ------------------------------------------------------------------------ */

void umcub_port_gpio_input(uint32_t pin, int pull);
bool umcub_port_gpio_read(uint32_t pin);
/* Alternate function, very-high speed, push-pull, no pull. */
void umcub_port_gpio_af(uint32_t pin);
/* Release a pin back to its reset state (analog / input). */
void umcub_port_gpio_reset(uint32_t pin);
/* Push-pull output (low speed) driven to `level`, and later level changes. */
void umcub_port_gpio_output(uint32_t pin, bool level);
void umcub_port_gpio_write(uint32_t pin, bool level);

/* ------------------------------------------------------------------------ */
/* Entropy and protection state                                              */
/* ------------------------------------------------------------------------ */

/* Raw entropy for nonces and back-off (conditioned by the caller with a hash).
 * Returns UMCUB_OK with `len` bytes, or an error. A true RNG where the family
 * has one, otherwise noise of an analog source and clock jitter. */
int umcub_port_entropy(uint8_t *buf, size_t len);
/* Flash readout protection level: 0 (none), 1 (debug/readout blocked),
 * 2 (permanent, H7 only). */
int umcub_port_rdp_level(void);

/* ------------------------------------------------------------------------ */
/* Internal flash. Addresses are absolute.                                   */
/* ------------------------------------------------------------------------ */

/* Program unit in bytes (writes must be aligned and a multiple of it). */
uint32_t umcub_flash_write_align(void);
uint8_t umcub_flash_erased_val(void);

/* Sector containing `addr`. Returns UMCUB_EINVAL if outside flash. */
int umcub_flash_sector_info(uint32_t addr, uint32_t *start, uint32_t *size);

int umcub_flash_read(uint32_t addr, void *dst, size_t len);
int umcub_flash_write(uint32_t addr, const void *src, size_t len);
/* `addr` and `len` must cover whole sectors. */
int umcub_flash_erase(uint32_t addr, size_t len);

/* ------------------------------------------------------------------------ */
/* Dual-core (only on families defining UMCUB_FAMILY_DUALCORE).              */
/* ------------------------------------------------------------------------ */

/* Main core: let the second core run the application whose vector table is
 * at `vtor` (UMCUB_DUALCORE_SINGLE_BOOT). */
void umcub_port_core2_release(uint32_t vtor);

/* PER_CORE handshake (hardware semaphores). */
void umcub_port_core_signal(unsigned event);
bool umcub_port_core_wait(unsigned event, uint32_t timeout_ms);
/* PER_CORE: main core leaves the clock tree running on deinit (the other
 * core's bootloader may still be using it). */
void umcub_port_keep_clocks(bool keep);
#define UMCUB_CORE_EVT_CLOCKS_READY  0   /* main core -> second core */
#define UMCUB_CORE_EVT_BOOT_DONE     1   /* second core -> main core */

#ifdef __cplusplus
}
#endif

#endif /* UMCUB_PORT_H */
