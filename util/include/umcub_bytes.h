/*
 * Big / little endian loads and stores at any (unaligned) address: memcpy
 * (one unaligned access on Cortex-M3 and up) plus the compiler's byte swap.
 */
#ifndef UMCUB_BYTES_H
#define UMCUB_BYTES_H

#include <stdint.h>
#include <string.h>

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define UMCUB_BE16(v) __builtin_bswap16(v)
#define UMCUB_BE32(v) __builtin_bswap32(v)
#define UMCUB_LE16(v) (v)
#define UMCUB_LE32(v) (v)
#else
#define UMCUB_BE16(v) (v)
#define UMCUB_BE32(v) (v)
#define UMCUB_LE16(v) __builtin_bswap16(v)
#define UMCUB_LE32(v) __builtin_bswap32(v)
#endif

static inline uint16_t umcub_get_be16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return UMCUB_BE16(v); }
static inline uint32_t umcub_get_be32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return UMCUB_BE32(v); }
static inline uint16_t umcub_get_le16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return UMCUB_LE16(v); }
static inline uint32_t umcub_get_le32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return UMCUB_LE32(v); }
static inline void umcub_put_be16(uint8_t *p, uint16_t v) { v = UMCUB_BE16(v); memcpy(p, &v, 2); }
static inline void umcub_put_be32(uint8_t *p, uint32_t v) { v = UMCUB_BE32(v); memcpy(p, &v, 4); }
static inline void umcub_put_le16(uint8_t *p, uint16_t v) { v = UMCUB_LE16(v); memcpy(p, &v, 2); }
static inline void umcub_put_le32(uint8_t *p, uint32_t v) { v = UMCUB_LE32(v); memcpy(p, &v, 4); }

#endif
