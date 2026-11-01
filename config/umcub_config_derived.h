/*
 * What is left for the C side to derive once the generated umcub_config.h and
 * the family facts are in: the macros below cannot be Kconfig values.
 */
#ifndef UMCUB_CONFIG_DERIVED_H
#define UMCUB_CONFIG_DERIVED_H

/* Family value a port may override in umcub_family_facts.h. */
#ifndef UMCUB_FAMILY_ERASED_VAL
#define UMCUB_FAMILY_ERASED_VAL         0xFF
#endif

/* Size of the erase sector holding `a` (layout checks); a family with sectors
 * of different sizes defines it itself. */
#if !defined(UMCUB_FAMILY_SECTOR_AT) && defined(UMCUB_FAMILY_UNIFORM_SECTOR)
#define UMCUB_FAMILY_SECTOR_AT(a)       UMCUB_FAMILY_UNIFORM_SECTOR
#endif

/* The inspection commands of UMCUB_CFG_CMD_TABLE ("+inspect_read"). */
#if UMCUB_CFG_INSPECT_VERIFY
#define UMCUB_DEFAULT_CMD_VERIFY        UMCUB_CMD("verify", UMCUB_CMD_VERIFY)
#else
#define UMCUB_DEFAULT_CMD_VERIFY
#endif
#if UMCUB_CFG_INSPECT_HASH
#define UMCUB_DEFAULT_CMD_HASH          UMCUB_CMD("hash", UMCUB_CMD_HASH)
#else
#define UMCUB_DEFAULT_CMD_HASH
#endif
#if UMCUB_CFG_READBACK
#define UMCUB_DEFAULT_CMD_READ          UMCUB_CMD("read", UMCUB_CMD_READ)
#else
#define UMCUB_DEFAULT_CMD_READ
#endif
#define UMCUB_CMD_INSPECT_DEFAULTS      UMCUB_DEFAULT_CMD_VERIFY UMCUB_DEFAULT_CMD_HASH UMCUB_DEFAULT_CMD_READ

/* "+inspect": verify / hash / read as text only without SMP: with it, hosts
 * check slots through the SMP inspect group (about 0.9 K less). */
#if UMCUB_CFG_SMP_ENABLE
#define UMCUB_DEFAULT_CMD_INSPECT
#else
#define UMCUB_DEFAULT_CMD_INSPECT       UMCUB_CMD_INSPECT_DEFAULTS
#endif

#endif /* UMCUB_CONFIG_DERIVED_H */
