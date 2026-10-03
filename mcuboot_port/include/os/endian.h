#ifndef UMCUB_OS_ENDIAN_H
#define UMCUB_OS_ENDIAN_H
#include <stdint.h>
#ifndef ntohs
#define ntohs(x) __builtin_bswap16((uint16_t)(x))
#define htons(x) __builtin_bswap16((uint16_t)(x))
#define ntohl(x) __builtin_bswap32((uint32_t)(x))
#define htonl(x) __builtin_bswap32((uint32_t)(x))
#endif
#endif
