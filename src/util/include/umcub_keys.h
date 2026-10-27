/*
 * umcub link SECURE keys, generated at build time from UMCUB_DEVICE_KEY /
 * UMCUB_HOST_KEY by `umcub keys` (tools/host/umcub, umcub_link_keys.c in the build
 * tree). Only linked with UMCUB_CFG_LINK_SECURE_ANY.
 */
#ifndef UMCUB_KEYS_H
#define UMCUB_KEYS_H

#include <stdint.h>

/* Device static key (private scalar, big endian): session ECDH, CSPRNG key. */
extern const uint8_t umcub_link_device_priv[32];
/* Admin public key (X || Y): verifies the host's AUTH signature. */
extern const uint8_t umcub_link_admin_pub[64];

#endif /* UMCUB_KEYS_H */
