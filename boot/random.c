/*
 * Conditioned random bytes (see umcub_random.h). Every 32-byte block is
 * SHA-256(counter | UID | millis | 32 bytes of port entropy): even with weak
 * entropy (STM32F1) two outputs never repeat within a boot, and the UID makes
 * them differ between devices.
 */
#include <string.h>
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "umcub_random.h"
#include "tinycrypt/sha256.h"

void umcub_random(void *out, size_t len)
{
    static uint32_t counter;
    uint8_t *o = out;
    while (len) {
        uint8_t uid[12], ent[32], digest[TC_SHA256_DIGEST_SIZE];
        uint32_t now = umcub_port_millis();
        umcub_port_uid(uid);
        if (umcub_port_entropy(ent, sizeof(ent)) != UMCUB_OK) {
            memset(ent, 0, sizeof(ent));        /* still unique: counter + time + UID */
        }
        struct tc_sha256_state_struct s;
        (void)tc_sha256_init(&s);
        counter++;
        (void)tc_sha256_update(&s, (const uint8_t *)&counter, sizeof(counter));
        (void)tc_sha256_update(&s, uid, sizeof(uid));
        (void)tc_sha256_update(&s, (const uint8_t *)&now, sizeof(now));
        (void)tc_sha256_update(&s, ent, sizeof(ent));
        (void)tc_sha256_final(digest, &s);
        size_t n = len < sizeof(digest) ? len : sizeof(digest);
        memcpy(o, digest, n);
        o += n;
        len -= n;
    }
}

uint32_t umcub_random_u32(void)
{
    uint32_t v;
    umcub_random(&v, sizeof(v));
    return v;
}
