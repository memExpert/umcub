/* UMCUB_CONFIG_PRE for host tests: direct-xip - the image runs from the slot it
 * is linked for, MCUboot boots the newest valid one; SMP uploads name slots. */
#define UMCUB_CFG_DUALCORE_MODE UMCUB_DUALCORE_NONE
#define UMCUB_CFG_UPGRADE_MODE  UMCUB_MODE_DIRECT_XIP
