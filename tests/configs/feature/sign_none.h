/* UMCUB_CONFIG_POST: images without a signature (SHA-256 TLV only): damaged
 * images are refused, anybody's are booted. For closed devices on small parts. */
#undef UMCUB_CFG_SIGNATURE
#define UMCUB_CFG_SIGNATURE             UMCUB_SIGN_NONE
