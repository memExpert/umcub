/* UMCUB_CONFIG_PRE for host tests: swap-scratch with UMCUB_SIGN_NONE - images
 * with a SHA-256 TLV only (damaged ones still refused). */
#define UMCUB_CFG_DUALCORE_MODE UMCUB_DUALCORE_NONE
#define UMCUB_CFG_UPGRADE_MODE  UMCUB_MODE_SWAP_SCRATCH
#define UMCUB_CFG_SIGNATURE     UMCUB_SIGN_NONE
