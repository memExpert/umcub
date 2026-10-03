/*
 * Conditioned random bytes (see umcub_random.h). Every 32-byte block is a hash
 * of: boot counter (kept over resets), call counter, UID, time and 32 bytes of
 * port entropy - keyed with the device key when the SECURE link is built, so
 * an outsider cannot predict outputs even from weak entropy (STM32F1).
 * Fails closed: no entropy, or entropy that fails a simple health test
 * (repeated block, too few distinct byte values), gives an error, and the
 * caller must not use the buffer.
 */
#include <stdbool.h>
#include <string.h>
#include "umcub_cfg.h"
#include "umcub_boot.h"
#include "umcub_port.h"
#include "umcub_random.h"
#include "tinycrypt/sha256.h"
#if UMCUB_CFG_LINK_SECURE_ANY
#include "tinycrypt/hmac.h"
extern const uint8_t umcub_link_device_priv[32];
#endif

#define ENT_LEN         32u
#define MIN_DISTINCT    8u          /* a healthy 32-byte block has ~28 distinct values */

static bool healthy(const uint8_t *ent)
{
    static uint8_t last[ENT_LEN];
    static bool have_last;
    if (have_last && memcmp(ent, last, ENT_LEN) == 0) {
        return false;                           /* stuck source */
    }
    memcpy(last, ent, ENT_LEN);
    have_last = true;
    uint32_t seen[8] = { 0 };
    unsigned distinct = 0;
    for (unsigned i = 0; i < ENT_LEN; i++) {
        uint32_t bit = 1u << (ent[i] & 31u);
        if (!(seen[ent[i] >> 5] & bit)) {
            seen[ent[i] >> 5] |= bit;
            distinct++;
        }
    }
    return distinct >= MIN_DISTINCT;
}

int umcub_random(void *out, size_t len)
{
    static uint32_t counter;
    uint8_t *o = out;
    uint32_t boot = umcub_handoff_boot_count();
    while (len) {
        uint8_t uid[12], ent[ENT_LEN], digest[TC_SHA256_DIGEST_SIZE];
        uint32_t now = umcub_port_millis();
        umcub_port_uid(uid);
        if (umcub_port_entropy(ent, sizeof(ent)) != UMCUB_OK || !healthy(ent)) {
            memset(out, 0, (size_t)(o - (uint8_t *)out));
            return UMCUB_EIO;
        }
        counter++;
#if UMCUB_CFG_LINK_SECURE_ANY
        struct tc_hmac_state_struct h;
        (void)tc_hmac_set_key(&h, umcub_link_device_priv, 32);
        (void)tc_hmac_init(&h);
        (void)tc_hmac_update(&h, &boot, sizeof(boot));
        (void)tc_hmac_update(&h, &counter, sizeof(counter));
        (void)tc_hmac_update(&h, uid, sizeof(uid));
        (void)tc_hmac_update(&h, &now, sizeof(now));
        (void)tc_hmac_update(&h, ent, sizeof(ent));
        (void)tc_hmac_final(digest, sizeof(digest), &h);
        memset(&h, 0, sizeof(h));
#else
        struct tc_sha256_state_struct s;
        (void)tc_sha256_init(&s);
        (void)tc_sha256_update(&s, (const uint8_t *)&boot, sizeof(boot));
        (void)tc_sha256_update(&s, (const uint8_t *)&counter, sizeof(counter));
        (void)tc_sha256_update(&s, uid, sizeof(uid));
        (void)tc_sha256_update(&s, (const uint8_t *)&now, sizeof(now));
        (void)tc_sha256_update(&s, ent, sizeof(ent));
        (void)tc_sha256_final(digest, &s);
#endif
        size_t n = len < sizeof(digest) ? len : sizeof(digest);
        memcpy(o, digest, n);
        o += n;
        len -= n;
    }
    return UMCUB_OK;
}

uint32_t umcub_random_u32(void)
{
    /* Back-off slots only (not a secret): fall back to the time on failure. */
    uint32_t v;
    if (umcub_random(&v, sizeof(v)) != UMCUB_OK) {
        v = umcub_port_millis() * 2654435761u;
    }
    return v;
}

/* tinycrypt's RNG hook: the random-Z blinding of the ECDH scalar
 * multiplication (ecc_dh.c is built with default_RNG_defined). */
int default_CSPRNG(uint8_t *dest, unsigned int size)
{
    return umcub_random(dest, size) == UMCUB_OK;
}
