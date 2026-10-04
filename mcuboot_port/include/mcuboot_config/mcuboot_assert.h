#ifndef UMCUB_MCUBOOT_ASSERT_H
#define UMCUB_MCUBOOT_ASSERT_H

#include "umcub_cfg.h"

/* The bootloader resets on a failed assertion (mcuboot_port/src/compat.c). */
#if defined(UMCUB_BUILDING_APP)
void umcub_assert_fail(const char *file, int line);
#else
__attribute__((noreturn)) void umcub_assert_fail(const char *file, int line);
#endif

/* Only the file name (not its path) and only when it can be logged: the
 * strings would otherwise stay in flash and depend on the checkout path. */
#if UMCUB_CFG_LOG_LEVEL == 0
#define UMCUB_ASSERT_FILE ((const char *)0)
#elif defined(__FILE_NAME__)
#define UMCUB_ASSERT_FILE __FILE_NAME__
#else
#define UMCUB_ASSERT_FILE __FILE__
#endif

#undef assert
#define assert(x) ((x) ? (void)0 : umcub_assert_fail(UMCUB_ASSERT_FILE, __LINE__))

#endif
