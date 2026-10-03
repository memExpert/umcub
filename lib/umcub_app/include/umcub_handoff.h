/*
 * Bootloader <-> application handoff area.
 *
 * Lives at UMCUB_CFG_SHARED_RAM_ADDR in RAM that is neither initialised by
 * the application's startup code nor cleared by a reset. Both sides only
 * trust it after checking magic/CRC.
 *
 *  - request part:  written by the application, consumed (cleared) by the
 *                   bootloader on the next boot.
 *  - info part:     written by the bootloader right before jumping to the
 *                   application, protected by a CRC-32.
 *  - core2 part:    dual-core release of the second core (bootloader only).
 */
#ifndef UMCUB_HANDOFF_H
#define UMCUB_HANDOFF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UMCUB_HANDOFF_MAGIC      0x42434D55u   /* "UMCB" */
#define UMCUB_HANDOFF_VERSION    1u
#define UMCUB_REQUEST_MAGIC      0x52514255u   /* "UBQR" */
#define UMCUB_CORE2_MAGIC        0x32455243u   /* "CRE2" */

/* umcub_handoff_t.request */
#define UMCUB_REQ_NONE           0u
#define UMCUB_REQ_ENTER_BOOT     1u            /* stay in recovery/update mode */
#define UMCUB_REQ_BOOT_APP       2u            /* boot the application, skip entry pin / wait window */

/* umcub_handoff_t.boot_reason */
#define UMCUB_BOOT_NORMAL        0u
#define UMCUB_BOOT_UPGRADED      1u            /* a new image was installed */
#define UMCUB_BOOT_REVERTED      2u            /* unconfirmed image reverted */
#define UMCUB_BOOT_AFTER_RECOVERY 3u

/* Transport ids (umcub_handoff_t.last_transport) */
#define UMCUB_TRANSPORT_NONE     0u
#define UMCUB_TRANSPORT_UART     1u
#define UMCUB_TRANSPORT_USB_CDC  2u
#define UMCUB_TRANSPORT_USB_DFU  3u
#define UMCUB_TRANSPORT_CAN      4u
#define UMCUB_TRANSPORT_ETH      5u
#define UMCUB_TRANSPORT_APP      6u

#define UMCUB_MAX_IMAGES         2u

typedef struct {
    uint8_t  major;
    uint8_t  minor;
    uint16_t revision;
    uint32_t build;
} umcub_version_t;

typedef struct {
    /* request part (application -> bootloader) */
    uint32_t request_magic;     /* UMCUB_REQUEST_MAGIC */
    uint32_t request;           /* UMCUB_REQ_* */
    uint32_t request_arg;       /* user value, echoed in boot log */
    uint32_t request_check;     /* ~request_magic ^ request */

    /* info part (bootloader -> application) */
    uint32_t magic;             /* UMCUB_HANDOFF_MAGIC */
    uint16_t version;           /* UMCUB_HANDOFF_VERSION */
    uint16_t size;              /* sizeof(umcub_handoff_t) */
    umcub_version_t boot_version;
    uint32_t boot_git_hash;     /* first 8 hex digits of the bootloader commit */
    uint8_t  reset_cause;       /* UMCUB_RESET_* */
    uint8_t  boot_reason;       /* UMCUB_BOOT_* */
    uint8_t  image_count;
    uint8_t  last_transport;    /* UMCUB_TRANSPORT_* */
    uint8_t  active_slot[UMCUB_MAX_IMAGES];   /* 0 primary, 1 secondary */
    uint8_t  upgrade_mode;      /* UMCUB_MODE_* */
    uint8_t  reserved0;
    umcub_version_t image_version[UMCUB_MAX_IMAGES];
    uint32_t image_addr[UMCUB_MAX_IMAGES];    /* start of the running slot */
    uint32_t reset_flags;       /* raw reset status register (cleared by the bootloader) */
    uint32_t crc32;             /* CRC-32 of magic..image_addr */

    /* dual-core release (bootloader internal) */
    uint32_t core2_magic;
    uint32_t core2_vtor;
} umcub_handoff_t;

#define UMCUB_HANDOFF_INFO_OFFSET   16u
#define UMCUB_HANDOFF_INFO_LEN      (__builtin_offsetof(umcub_handoff_t, crc32) - UMCUB_HANDOFF_INFO_OFFSET)

_Static_assert(__builtin_offsetof(umcub_handoff_t, magic) == UMCUB_HANDOFF_INFO_OFFSET, "layout");
_Static_assert(sizeof(umcub_handoff_t) <= 256, "handoff area is 256 bytes");
/* Written with 32-bit stores only (ECC SRAM: narrower writes are lost on reset). */
_Static_assert(sizeof(umcub_handoff_t) % 4 == 0 && __builtin_offsetof(umcub_handoff_t, core2_magic) % 4 == 0, "word layout");

/* Bootloader info block at UMCUB_CFG_BOOT_INFO_ADDR in the bootloader's own
 * flash: readable by the application even if the handoff RAM was lost. */
#define UMCUB_INFO_MAGIC         0x4F464E49u   /* "INFO" */

typedef struct {
    uint32_t magic;             /* UMCUB_INFO_MAGIC */
    uint16_t version;           /* layout version, 1 */
    uint16_t size;
    umcub_version_t boot_version;
    uint32_t git_hash;
    uint8_t  upgrade_mode;      /* UMCUB_MODE_* */
    uint8_t  image_count;
    uint8_t  dualcore_mode;
    uint8_t  transports;        /* bit n = UMCUB_TRANSPORT n compiled in */
    uint32_t slot_addr[UMCUB_MAX_IMAGES][2];  /* [image][primary/secondary] */
    uint32_t slot_size[UMCUB_MAX_IMAGES][2];
    uint32_t header_size;       /* image header size (vector table offset) */
    char     board[24];
} umcub_info_block_t;

/* CRC-32 (IEEE 802.3, reflected, init/xorout 0xFFFFFFFF). */
static inline uint32_t umcub_crc32(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

#ifdef __cplusplus
}
#endif

#endif /* UMCUB_HANDOFF_H */
