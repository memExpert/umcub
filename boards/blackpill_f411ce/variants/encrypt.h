/* Overlay (POST), Black Pill F411: encrypted images over UART. Needs more room:
 * bootloader in sectors 0-2 (48 KiB), slots unchanged; no in-place decryption
 * on the F4 (encrypted updates go through the secondary slot). */
#undef UMCUB_CFG_BOOT_SIZE
#define UMCUB_CFG_BOOT_SIZE             UMCUB_KB(48)
#undef UMCUB_CFG_ENCRYPT_IMAGES
#define UMCUB_CFG_ENCRYPT_IMAGES        1
