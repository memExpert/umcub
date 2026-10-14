/* Overlay (POST): encrypted images (MCUboot ECIES-P256 with the device key)
 * and readback, which then answers only inside an encrypted umcub link session
 * (SECURE + encryption on every transport, see link_secure.h). */
#include "link_secure.h"
#undef UMCUB_CFG_ENCRYPT_IMAGES
#define UMCUB_CFG_ENCRYPT_IMAGES        1
#undef UMCUB_CFG_READBACK
#define UMCUB_CFG_READBACK              1
