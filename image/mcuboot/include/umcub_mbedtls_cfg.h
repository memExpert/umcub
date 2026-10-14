/* mbedTLS subset: only the ASN.1 parser (ECDSA signature/key decoding). */
#ifndef UMCUB_MBEDTLS_CFG_H
#define UMCUB_MBEDTLS_CFG_H
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS
#define MBEDTLS_ASN1_PARSE_C
#endif
