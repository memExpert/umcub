/*
 * Random bytes for nonces and back-off: SHA-256 over a counter, the device UID,
 * the time and fresh port entropy (umcub_port_entropy()).
 */
#ifndef UMCUB_RANDOM_H
#define UMCUB_RANDOM_H

#include <stddef.h>
#include <stdint.h>

/* UMCUB_OK, or an error (no healthy entropy): then the output must not be used. */
int umcub_random(void *out, size_t len);
uint32_t umcub_random_u32(void);

#endif
