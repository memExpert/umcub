/* UMCUB_CONFIG_PRE for host tests: overwrite (the F1 / F4 default) - the
 * secondary slot is copied over the primary, no test boot, no revert. */
#define UMCUB_CFG_DUALCORE_MODE UMCUB_DUALCORE_NONE
#define UMCUB_CFG_UPGRADE_MODE  UMCUB_MODE_OVERWRITE
