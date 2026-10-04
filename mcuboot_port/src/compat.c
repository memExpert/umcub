/*
 * Small runtime pieces MCUboot's portable code expects from its host OS
 * (mynewt-style APIs used by boot_serial.c, assert, delays).
 */
#include <stdint.h>
#include <string.h>
#include "umcub_port.h"
#include "umcub_log.h"
#include "crc/crc16.h"
#include "base64/base64.h"
#include "os/os_cputime.h"
#include "hal/hal_system.h"
#include "umcub_transport.h"

uint16_t crc16_ccitt(uint16_t crc, const void *buf, int len)
{
    const uint8_t *p = buf;
    while (len-- > 0) {
        crc ^= (uint16_t)(*p++) << 8;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64_encode(const void *data, int size, char *s, uint8_t should_pad)
{
    const uint8_t *in = data;
    int o = 0;
    for (int i = 0; i < size; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        int n = size - i;
        if (n > 1) {
            v |= (uint32_t)in[i + 1] << 8;
        }
        if (n > 2) {
            v |= in[i + 2];
        }
        s[o++] = b64[(v >> 18) & 63];
        s[o++] = b64[(v >> 12) & 63];
        if (n > 1) {
            s[o++] = b64[(v >> 6) & 63];
        } else if (should_pad) {
            s[o++] = '=';
        }
        if (n > 2) {
            s[o++] = b64[v & 63];
        } else if (should_pad) {
            s[o++] = '=';
        }
    }
    s[o] = '\0';
    return o;
}

static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int b64_len(const char *str)
{
    int n = 0;
    while (str[n] && str[n] != '\n' && str[n] != '\r') {
        n++;
    }
    return n;
}

int base64_decode_len(const char *str)
{
    return (b64_len(str) * 3) / 4 + 1;
}

int base64_decode(const char *str, void *data)
{
    uint8_t *out = data;
    int len = b64_len(str), o = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (int i = 0; i < len; i++) {
        if (str[i] == '=') {
            break;
        }
        int v = b64_val(str[i]);
        if (v < 0) {
            return -1;
        }
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (uint8_t)(acc >> bits);
        }
    }
    return o;
}

/* Used by boot_serial before resetting: keep the transports running so the
 * reset response actually leaves (USB, Ethernet need polling). */
void os_cputime_delay_usecs(uint32_t usecs)
{
    uint32_t start = umcub_port_millis();
    while ((uint32_t)(umcub_port_millis() - start) < (usecs + 999u) / 1000u) {
        umcub_transports_poll();
    }
}

void hal_system_reset(void)
{
    umcub_port_reset();
}

void umcub_assert_fail(const char *file, int line)
{
    UMCUB_LOG_ERR("assert %s:%d", file ? file : "?", line);
    (void)file;
    (void)line;
    umcub_port_reset();
}

#ifndef UMCUB_HOST_TEST
/* newlib's assert() handler pulls in fprintf; keep it small. */
void __assert_func(const char *file, int line, const char *func, const char *expr)
{
    (void)func;
    (void)expr;
    umcub_assert_fail(file, line);
    for (;;) {
    }
}
#endif

/* Keep newlib stdio out of the image: MCUboot/zcbor only need these. */
#ifndef UMCUB_HOST_TEST
#include <stdarg.h>
#include <stdio.h>

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = umcub_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    return umcub_vsnprintf(buf, size, fmt, ap);
}

int printf(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}

int puts(const char *s)
{
    (void)s;
    return 0;
}
#endif /* !UMCUB_HOST_TEST */
