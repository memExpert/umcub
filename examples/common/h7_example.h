/* Minimal board helpers for the NUCLEO-H755ZI-Q examples (CMSIS only). */
#ifndef H7_EXAMPLE_H
#define H7_EXAMPLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void h7_led_init(char port, int pin);
void h7_led_toggle(char port, int pin);
void h7_delay_ms(uint32_t ms);              /* SysTick polled, no interrupt */

/* USART3 (ST-LINK VCP) 115200 8N1 on the reset clock (HSI 64 MHz); its
 * ex_putc() / ex_getc() are those of umcub_example.h. */
void h7_uart_init(void);

#endif
