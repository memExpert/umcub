/*
 * MCUboot configuration for umcub. The ONLY place where UMCUB_CFG_* options
 * are translated into MCUBOOT_* macros.
 */
#ifndef UMCUB_MCUBOOT_CONFIG_H
#define UMCUB_MCUBOOT_CONFIG_H

#include "umcub_cfg.h"

/* --- upgrade strategy ---------------------------------------------------- */
#if UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_OVERWRITE
#define MCUBOOT_OVERWRITE_ONLY
#define MCUBOOT_OVERWRITE_ONLY_FAST
#elif UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_SWAP_SCRATCH
#define MCUBOOT_SWAP_USING_SCRATCH 1
#elif UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_SWAP_MOVE
#define MCUBOOT_SWAP_USING_MOVE 1
#elif UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_SWAP_OFFSET
#define MCUBOOT_SWAP_USING_OFFSET 1
#elif UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_DIRECT_XIP
#define MCUBOOT_DIRECT_XIP
#elif UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_DIRECT_XIP_REVERT
#define MCUBOOT_DIRECT_XIP
#define MCUBOOT_DIRECT_XIP_REVERT
#endif

/* --- signature / crypto -------------------------------------------------- */
#if UMCUB_CFG_SIGNATURE == UMCUB_SIGN_EC256
#define MCUBOOT_SIGN_EC256
#define MCUBOOT_USE_TINYCRYPT
#elif UMCUB_CFG_SIGNATURE == UMCUB_SIGN_NONE
#define MCUBOOT_USE_TINYCRYPT     /* SHA-256 only */
#else
#error "umcub: unsupported UMCUB_CFG_SIGNATURE"
#endif

/* The application library never decrypts; the trailer fields it writes do
 * not depend on the encryption keys stored below them. */
#if UMCUB_CFG_ENCRYPT_IMAGES && !defined(UMCUB_BUILDING_APP)
#define MCUBOOT_ENC_IMAGES
#define MCUBOOT_ENCRYPT_EC256
#endif

#if UMCUB_CFG_VALIDATE_PRIMARY
#define MCUBOOT_VALIDATE_PRIMARY_SLOT
#endif
#if UMCUB_CFG_DOWNGRADE_PREVENTION
#if UMCUB_CFG_UPGRADE_MODE >= UMCUB_MODE_DIRECT_XIP
#error "umcub: MCUboot has no downgrade prevention in direct-xip modes (newest valid image wins)"
#endif
#define MCUBOOT_DOWNGRADE_PREVENTION
#define MCUBOOT_DOWNGRADE_PREVENTION_SECURITY_COUNTER 0   /* compare versions, not security counters */
#endif

#if UMCUB_CFG_UPGRADE_MODE >= UMCUB_MODE_DIRECT_XIP && defined(UMCUB_BUILDING_APP)
/* Application side of bootutil_public: boot_set_next() for direct-xip. */
#define MCUBOOT_BOOTUTIL_LIB_FOR_DIRECT_XIP
#endif

/* --- images / flash ------------------------------------------------------ */
#define MCUBOOT_IMAGE_NUMBER            UMCUB_CFG_IMAGE_NUMBER
#define MCUBOOT_USE_FLASH_AREA_GET_SECTORS
#define MCUBOOT_BOOT_MAX_ALIGN          UMCUB_FAMILY_WRITE_ALIGN

#define UMCUB_MAX_SLOT_SIZE_ \
    ((UMCUB_CFG_IMG0_PRIMARY_SIZE > UMCUB_CFG_IMG0_SECONDARY_SIZE) ? UMCUB_CFG_IMG0_PRIMARY_SIZE : UMCUB_CFG_IMG0_SECONDARY_SIZE)
#define UMCUB_MAX_SLOT_SIZE \
    ((UMCUB_MAX_SLOT_SIZE_ > UMCUB_CFG_IMG1_PRIMARY_SIZE) ? \
     ((UMCUB_MAX_SLOT_SIZE_ > UMCUB_CFG_IMG1_SECONDARY_SIZE) ? UMCUB_MAX_SLOT_SIZE_ : UMCUB_CFG_IMG1_SECONDARY_SIZE) : \
     ((UMCUB_CFG_IMG1_PRIMARY_SIZE > UMCUB_CFG_IMG1_SECONDARY_SIZE) ? UMCUB_CFG_IMG1_PRIMARY_SIZE : UMCUB_CFG_IMG1_SECONDARY_SIZE))
#if defined(UMCUB_FAMILY_MIN_SECTOR)
#define MCUBOOT_MAX_IMG_SECTORS         (UMCUB_MAX_SLOT_SIZE / UMCUB_FAMILY_MIN_SECTOR)
#else
#define MCUBOOT_MAX_IMG_SECTORS         256
#endif

/* --- serial recovery (SMP), used by every umcub transport ---------------- */
#define MCUBOOT_SERIAL_MAX_RECEIVE_SIZE UMCUB_CFG_SMP_MTU
#if UMCUB_CFG_UPGRADE_MODE < UMCUB_MODE_DIRECT_XIP
/* Image state write marks the secondary slot pending: swap modes only. */
#define MCUBOOT_SERIAL_IMG_GRP_IMAGE_STATE
#endif
#define MCUBOOT_SERIAL_IMG_GRP_HASH
#define MCUBOOT_SERIAL_IMG_GRP_SLOT_INFO
#define MCUBOOT_BOOT_MGMT_ECHO
#if UMCUB_CFG_INSPECT && UMCUB_CFG_SMP
#define MCUBOOT_PERUSER_MGMT_GROUP_ENABLED 1   /* verify/hash/read group, boot/smp_inspect.c */
#else
#define MCUBOOT_PERUSER_MGMT_GROUP_ENABLED 0
#endif
/* Erase sector by sector while receiving instead of the whole slot on the
 * first chunk (seconds on 128 KiB sectors, hosts time out meanwhile). */
#define MCUBOOT_ERASE_PROGRESSIVELY

#if UMCUB_CFG_SMP_DIRECT_UPLOAD || UMCUB_CFG_UPGRADE_MODE >= UMCUB_MODE_DIRECT_XIP
/* SMP upload "image" N selects a slot: 0/1 image 0 primary, 2 image 0
 * secondary, 3 image 1 primary, 4 image 1 secondary (Zephyr numbering).
 * Without it, "image" N uploads into image N's primary slot. Always on in
 * direct-xip: an image linked for a slot must be uploaded into that slot. */
#define MCUBOOT_SERIAL_DIRECT_IMAGE_UPLOAD
#endif

/* --- logging / misc ------------------------------------------------------ */
#if UMCUB_CFG_LOG_LEVEL > 0 && !defined(UMCUB_BUILDING_APP)
#define MCUBOOT_HAVE_LOGGING 1
#endif
#define MCUBOOT_HAVE_ASSERT_H 1
#define MCUBOOT_USE_SNPRINTF 0

void umcub_port_wdg_feed(void);
void umcub_port_idle(void);
/* mcuboot_port/src/hooks.c: board type check before an image is installed,
 * validation of encrypted SMP uploads, "was an update performed" for the boot
 * reason. Not in the application library: bootutil_public.c needs no hooks. */
#if !defined(UMCUB_BUILDING_APP)
#define MCUBOOT_IMAGE_ACCESS_HOOKS
#endif

#define MCUBOOT_WATCHDOG_FEED()         umcub_port_wdg_feed()
#define MCUBOOT_CPU_IDLE()              umcub_port_idle()

#endif /* UMCUB_MCUBOOT_CONFIG_H */
