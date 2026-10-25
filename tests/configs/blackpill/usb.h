/* Overlay (POST) for boards/blackpill_f411ce: USB CDC (SMP) + USB DFU on the
 * USB-C socket (OTG FS PA11/PA12) next to UART. Needs the 25 MHz crystal
 * (48 MHz from the PLL) and more room: bootloader 48 KiB (sectors 0-2). */
#undef UMCUB_CFG_BOOT_SIZE
#define UMCUB_CFG_BOOT_SIZE             UMCUB_KB(48)

#define UMCUB_CFG_TRANSPORT_USB_CDC     1
#define UMCUB_CFG_TRANSPORT_USB_DFU     1
