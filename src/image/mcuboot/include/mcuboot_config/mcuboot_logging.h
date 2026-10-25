#ifndef UMCUB_MCUBOOT_LOGGING_H
#define UMCUB_MCUBOOT_LOGGING_H

#include "umcub_log.h"

#define MCUBOOT_LOG_MODULE_DECLARE(...)
#define MCUBOOT_LOG_MODULE_REGISTER(...)

/* UMCUB_CFG_MCUBOOT_LOG_LEVEL limits MCUboot's messages below the umcub log
 * level (e.g. umcub info, MCUboot errors only). */
#define UMCUB_MCUBOOT_LOG(n) (UMCUB_CFG_MCUBOOT_LOG_LEVEL >= (n) && UMCUB_CFG_LOG_LEVEL >= (n))
#if UMCUB_MCUBOOT_LOG(1)
#define MCUBOOT_LOG_ERR(fmt, ...) umcub_log('E', fmt, ##__VA_ARGS__)
#else
#define MCUBOOT_LOG_ERR(fmt, ...) do { } while (0)
#endif
#if UMCUB_MCUBOOT_LOG(2)
#define MCUBOOT_LOG_WRN(fmt, ...) umcub_log('W', fmt, ##__VA_ARGS__)
#else
#define MCUBOOT_LOG_WRN(fmt, ...) do { } while (0)
#endif
#if UMCUB_MCUBOOT_LOG(3)
#define MCUBOOT_LOG_INF(fmt, ...) umcub_log('I', fmt, ##__VA_ARGS__)
#else
#define MCUBOOT_LOG_INF(fmt, ...) do { } while (0)
#endif
#if UMCUB_MCUBOOT_LOG(4)
#define MCUBOOT_LOG_DBG(fmt, ...) umcub_log('D', fmt, ##__VA_ARGS__)
#else
#define MCUBOOT_LOG_DBG(fmt, ...) do { } while (0)
#endif
#define MCUBOOT_LOG_SIM(...)

#endif
