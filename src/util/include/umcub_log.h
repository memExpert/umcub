/*
 * Tiny logger: printf subset (%d %i %u %x %X %p %s %c %%, l/ll/z/j/h
 * modifiers, width, '0' and '-' flags), no newlib stdio.
 */
#ifndef UMCUB_LOG_H
#define UMCUB_LOG_H

#include <stdarg.h>
#include <stddef.h>
#include "umcub_cfg.h"

typedef void (*umcub_log_sink_t)(const char *s, size_t len);

void umcub_log_set_sink(umcub_log_sink_t sink);
void umcub_log(char level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int umcub_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int umcub_snprintf(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

#if UMCUB_CFG_LOG_LEVEL >= 1
#define UMCUB_LOG_ERR(fmt, ...) umcub_log('E', fmt, ##__VA_ARGS__)
#else
#define UMCUB_LOG_ERR(fmt, ...) do { } while (0)
#endif
#if UMCUB_CFG_LOG_LEVEL >= 2
#define UMCUB_LOG_WRN(fmt, ...) umcub_log('W', fmt, ##__VA_ARGS__)
#else
#define UMCUB_LOG_WRN(fmt, ...) do { } while (0)
#endif
#if UMCUB_CFG_LOG_LEVEL >= 3
#define UMCUB_LOG_INF(fmt, ...) umcub_log('I', fmt, ##__VA_ARGS__)
#else
#define UMCUB_LOG_INF(fmt, ...) do { } while (0)
#endif
#if UMCUB_CFG_LOG_LEVEL >= 4
#define UMCUB_LOG_DBG(fmt, ...) umcub_log('D', fmt, ##__VA_ARGS__)
#else
#define UMCUB_LOG_DBG(fmt, ...) do { } while (0)
#endif

#endif /* UMCUB_LOG_H */
