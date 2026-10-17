/* Host test variant: encrypted images with the plain AES keys in the trailer
 * (MCUboot's default, UMCUB_CFG_SWAP_SAVE_ENCTLV 0). */
#include "cfg_swap_scratch_enc.h"
#define UMCUB_CFG_SWAP_SAVE_ENCTLV 0
