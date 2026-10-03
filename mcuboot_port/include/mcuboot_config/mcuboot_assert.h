#ifndef UMCUB_MCUBOOT_ASSERT_H
#define UMCUB_MCUBOOT_ASSERT_H

void umcub_assert_fail(const char *file, int line);

#undef assert
#define assert(x) ((x) ? (void)0 : umcub_assert_fail(__FILE__, __LINE__))

#endif
