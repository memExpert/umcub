/*
 * tinyUSB configuration for the umcub USB transport (device only, no OS).
 */
#ifndef UMCUB_TUSB_CONFIG_H
#define UMCUB_TUSB_CONFIG_H

#include "umcub_cfg.h"

#ifndef CFG_TUSB_MCU
#error "CFG_TUSB_MCU must be provided by the family (UMCUB_FAMILY_TUSB_MCU)"
#endif

#define CFG_TUSB_OS                 OPT_OS_NONE
#define CFG_TUSB_DEBUG              0
#define CFG_TUD_ENABLED             1
#define CFG_TUD_MAX_SPEED           OPT_MODE_FULL_SPEED
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN          __attribute__((aligned(4)))
#define CFG_TUD_ENDPOINT0_SIZE      64

#define CFG_TUD_CDC                 UMCUB_CFG_TRANSPORT_USB_CDC
#define CFG_TUD_DFU                 UMCUB_CFG_TRANSPORT_USB_DFU
#define CFG_TUD_MSC                 0
#define CFG_TUD_HID                 0
#define CFG_TUD_MIDI                0
#define CFG_TUD_VENDOR              0

#define CFG_TUD_CDC_RX_BUFSIZE      512
#define CFG_TUD_CDC_TX_BUFSIZE      512
#define CFG_TUD_CDC_EP_BUFSIZE      64

/* DFU block size: multiple of the flash program unit. */
#define CFG_TUD_DFU_XFER_BUFSIZE    1024

#endif
