/*
 * The board-independent part of the umcub console examples: a text console on
 * a UART with the keys below, the info umcub_boot_info() gives and the 'u'
 * upload protocol of examples/common/app_upload.py. A board's example provides the
 * ex_* functions of the first block (clock, pins, UART, LED, watchdog) and
 * calls ex_run().
 *   i  show bootloader / image information
 *   c  confirm this image (swap modes: otherwise reverted on next reset)
 *   b  reboot into the bootloader's recovery mode
 *   u  receive an image and write it into a slot (examples/common/app_upload.py)
 *   w  stop feeding the watchdog (UMCUB_CFG_WATCHDOG_MS: reset follows)
 *   r  reset
 */
#ifndef UMCUB_EXAMPLE_H
#define UMCUB_EXAMPLE_H

#include <stdbool.h>
#include <stdint.h>

/* --- provided by the board's example ------------------------------------- */
void ex_putc(char c);               /* send one byte (waits for room) */
bool ex_getc(uint8_t *c);           /* a received byte, without waiting */
uint32_t ex_millis(void);           /* milliseconds since start */
void ex_led_toggle(void);
void ex_watchdog_feed(void);        /* the IWDG the bootloader may have started */

/* --- common part (umcub_example.c) ---------------------------------------- */
void ex_puts(const char *s);        /* '\n' sent as "\r\n" */
void ex_put_u32(uint32_t v);
void ex_put_hex(uint32_t v);
/* Hands APP_NODE_ADDR to the bootloader (if defined), prints the info, then
 * serves the keys and blinks the LED (250 ms); `name` goes into the first line
 * ("app 1.0.0 (<name>)"). */
__attribute__((noreturn)) void ex_run(const char *name);

#endif /* UMCUB_EXAMPLE_H */
