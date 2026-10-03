/* Minimal board helpers for the NUCLEO-H755ZI-Q examples (CMSIS only). */
#ifndef H7_EXAMPLE_H
#define H7_EXAMPLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void ex_led_init(char port, int pin);
void ex_led_toggle(char port, int pin);
void ex_delay_ms(uint32_t ms);

/* USART3 (ST-LINK VCP) 115200 8N1 on the reset clock (HSI 64 MHz). */
void ex_uart_init(void);
bool ex_uart_getc(uint8_t *c);
void ex_uart_putc(char c);
void ex_puts(const char *s);
void ex_put_u32(uint32_t v);
void ex_put_hex(uint32_t v);

#endif
