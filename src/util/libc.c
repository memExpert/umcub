/*
 * Small C runtime of the bootloader: assert handlers and the stdio entry
 * points MCUboot / zcbor use, without newlib's stdio.
 */
#include <stdint.h>
#include <string.h>
#include "umcub_port.h"
#include "umcub_log.h"

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
