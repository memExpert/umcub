#ifndef UMCUB_MCUBOOT_LOGGING_H
#define UMCUB_MCUBOOT_LOGGING_H

#include "umcub_log.h"

#define MCUBOOT_LOG_MODULE_DECLARE(...)
#define MCUBOOT_LOG_MODULE_REGISTER(...)

#define MCUBOOT_LOG_ERR(fmt, ...) UMCUB_LOG_ERR(fmt, ##__VA_ARGS__)
#define MCUBOOT_LOG_WRN(fmt, ...) UMCUB_LOG_WRN(fmt, ##__VA_ARGS__)
#define MCUBOOT_LOG_INF(fmt, ...) UMCUB_LOG_INF(fmt, ##__VA_ARGS__)
#define MCUBOOT_LOG_DBG(fmt, ...) UMCUB_LOG_DBG(fmt, ##__VA_ARGS__)
#define MCUBOOT_LOG_SIM(...)

#endif
