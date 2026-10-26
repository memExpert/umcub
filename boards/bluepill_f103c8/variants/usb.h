/* Overlay (POST) for boards/bluepill_f103c8: USB CDC (SMP) + USB DFU next to
 * UART. Needs more room: bootloader 44 KiB + two 10 KiB slots. */
#undef UMCUB_CFG_BOOT_SIZE
#undef UMCUB_CFG_IMG0_PRIMARY_ADDR
#undef UMCUB_CFG_IMG0_PRIMARY_SIZE
#undef UMCUB_CFG_IMG0_SECONDARY_ADDR
#undef UMCUB_CFG_IMG0_SECONDARY_SIZE
#define UMCUB_CFG_BOOT_SIZE             UMCUB_KB(44)
#define UMCUB_CFG_IMG0_PRIMARY_ADDR     0x0800B000
#define UMCUB_CFG_IMG0_PRIMARY_SIZE     UMCUB_KB(10)
#define UMCUB_CFG_IMG0_SECONDARY_ADDR   0x0800D800
#define UMCUB_CFG_IMG0_SECONDARY_SIZE   UMCUB_KB(10)

#define UMCUB_CFG_TRANSPORT_USB_CDC     1
#define UMCUB_CFG_TRANSPORT_USB_DFU     1

/* No wait window: USB would attach for the window and vanish again on every
 * boot, which some host controllers do not survive (the D+ pull-up is fixed).
 * Recovery: request from the application or no valid image. */
#undef UMCUB_CFG_ENTRY_WAIT_MS
#define UMCUB_CFG_ENTRY_WAIT_MS         0
