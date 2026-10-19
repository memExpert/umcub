/*
 * Status LED (UMCUB_CFG_STATUS_LED): what the bootloader is doing, without a
 * log. Driven from the polling loop while transports listen (wait window,
 * recovery); untouched on a plain boot.
 *   slow blink (1 Hz)        listening: recovery mode or the wait window
 *   fast flicker             data arriving
 *   N blinks, pause          transport N (UMCUB_TRANSPORT_*) did not come up
 */
#ifndef UMCUB_LED_H
#define UMCUB_LED_H

#include "umcub_cfg.h"

#if UMCUB_CFG_STATUS_LED
void umcub_led_poll(void);
void umcub_led_activity(void);
void umcub_led_error(unsigned code);    /* the first code is shown */
void umcub_led_off(void);               /* pin back to its reset state */
#else
static inline void umcub_led_poll(void) {}
static inline void umcub_led_activity(void) {}
static inline void umcub_led_error(unsigned code) { (void)code; }
static inline void umcub_led_off(void) {}
#endif

#endif /* UMCUB_LED_H */
