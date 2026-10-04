/*
 * umcub logger and printf subset.
 */
#include <stdint.h>
#include "umcub_log.h"
#include "umcub_port.h"

static umcub_log_sink_t log_sink;

void umcub_log_set_sink(umcub_log_sink_t sink)
{
    log_sink = sink;
}

struct out {
    char *buf;
    size_t size;
    size_t n;
};

static void put(struct out *o, char c)
{
    if (o->n + 1 < o->size) {
        o->buf[o->n] = c;
    }
    o->n++;
}

/* *v /= base, returns the remainder - with 32-bit divisions only, so the
 * 64-bit division helpers of libgcc (~0.8 K) stay out of the image. */
static unsigned divmod(unsigned long long *v, unsigned base)
{
    uint32_t hi = (uint32_t)(*v >> 32), lo = (uint32_t)*v;
    uint32_t qh = hi / base, r = hi % base;
    uint32_t cur = (r << 16) | (lo >> 16);
    uint32_t qm = cur / base;
    cur = ((cur % base) << 16) | (lo & 0xFFFFu);
    *v = ((unsigned long long)qh << 32) | (qm << 16) | (cur / base);
    return cur % base;
}

static void put_num(struct out *o, unsigned long long v, unsigned base, bool upper,
                    bool neg, int width, char pad, bool left)
{
    char tmp[24];
    int len = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do {
        tmp[len++] = digits[divmod(&v, base)];
    } while (v);
    if (neg) {
        if (pad == '0') {
            put(o, '-');
        } else {
            tmp[len++] = '-';
        }
        width--;
    }
    int ndig = len;
    if (!left) {
        for (int i = ndig; i < width; i++) {
            put(o, pad);
        }
    }
    while (len) {
        put(o, tmp[--len]);
    }
    if (left) {
        for (int i = ndig; i < width; i++) {
            put(o, ' ');
        }
    }
}

int umcub_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    struct out o = { buf, size, 0 };
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            put(&o, *fmt);
            continue;
        }
        fmt++;
        bool left = false;
        char pad = ' ';
        for (;; fmt++) {
            if (*fmt == '-') {
                left = true;
            } else if (*fmt == '0') {
                pad = '0';
            } else {
                break;
            }
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt++ - '0');
        }
        int lng = 0;
        for (;; fmt++) {
            if (*fmt == 'l') {
                lng++;
            } else if (*fmt == 'j') {
                lng = 2;
            } else if (*fmt == 'z' || *fmt == 'h' || *fmt == 't') {
                /* size_t/short/ptrdiff are <= long on Cortex-M */
            } else {
                break;
            }
        }
        switch (*fmt) {
        case 'd':
        case 'i': {
            long long v = lng >= 2 ? va_arg(ap, long long) : lng ? va_arg(ap, long) : va_arg(ap, int);
            put_num(&o, v < 0 ? (unsigned long long)-v : (unsigned long long)v, 10, false, v < 0, width, pad, left);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            unsigned long long v = lng >= 2 ? va_arg(ap, unsigned long long)
                                 : lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
            put_num(&o, v, *fmt == 'u' ? 10 : 16, *fmt == 'X', false, width, pad, left);
            break;
        }
        case 'p':
            put(&o, '0');
            put(&o, 'x');
            put_num(&o, (uintptr_t)va_arg(ap, void *), 16, false, false, 8, '0', false);
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) {
                s = "(null)";
            }
            int len = 0;
            while (s[len]) {
                len++;
            }
            if (!left) {
                for (int i = len; i < width; i++) {
                    put(&o, ' ');
                }
            }
            while (*s) {
                put(&o, *s++);
            }
            if (left) {
                for (int i = len; i < width; i++) {
                    put(&o, ' ');
                }
            }
            break;
        }
        case 'c':
            put(&o, (char)va_arg(ap, int));
            break;
        case '%':
            put(&o, '%');
            break;
        case '\0':
            fmt--;
            break;
        default:
            put(&o, '%');
            put(&o, *fmt);
            break;
        }
    }
    if (size) {
        buf[o.n < size ? o.n : size - 1] = '\0';
    }
    return (int)o.n;
}

int umcub_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = umcub_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

void umcub_log(char level, const char *fmt, ...)
{
    if (!log_sink) {
        return;
    }
    char line[160];
    uint32_t ms = umcub_port_millis();
    int n = umcub_snprintf(line, sizeof(line), "[%5lu.%03lu] %c: ",
                           (unsigned long)(ms / 1000u), (unsigned long)(ms % 1000u), level);
    va_list ap;
    va_start(ap, fmt);
    n += umcub_vsnprintf(line + n, sizeof(line) - (size_t)n - 2, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof(line) - 3) {
        n = sizeof(line) - 3;
    }
    line[n++] = '\r';
    line[n++] = '\n';
    log_sink(line, (size_t)n);
}
