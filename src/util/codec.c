/*
 * CRC-16/CCITT and base64 (NLIP framing, umcub link lines, lite upload frames;
 * the prototypes are the ones MCUboot's boot_serial expects).
 */
#include <stdint.h>
#include "crc/crc16.h"
#include "base64/base64.h"

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
