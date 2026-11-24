/*
 * USB descriptors: CDC-ACM (SMP serial) and/or DFU (one alt setting per
 * image, downloading into that image's secondary slot).
 */
#include <string.h>
#include "tusb.h"
#include "umcub_cfg.h"
#include "umcub_port.h"

enum {
#if CFG_TUD_CDC
    ITF_CDC_CTRL,
    ITF_CDC_DATA,
#endif
#if CFG_TUD_DFU
    ITF_DFU,
#endif
    ITF_COUNT
};

enum {
    STR_LANG,
    STR_MANUFACTURER,
    STR_PRODUCT,
    STR_SERIAL,
    STR_CDC,
    STR_DFU_ALT0,
};

#define EP_CDC_NOTIF   0x81
#define EP_CDC_OUT     0x02
#define EP_CDC_IN      0x82

#define CONFIG_LEN (TUD_CONFIG_DESC_LEN + CFG_TUD_CDC * TUD_CDC_DESC_LEN + \
                    CFG_TUD_DFU * TUD_DFU_DESC_LEN(UMCUB_CFG_IMAGE_NUMBER))

static const tusb_desc_device_t device_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = CFG_TUD_DFU ? 0x0210 : 0x0200,    /* 2.10: a BOS descriptor (MS OS 2.0, below) */
#if CFG_TUD_CDC
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
#else
    .bDeviceClass = 0,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
#endif
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = UMCUB_CFG_USB_VID,
    .idProduct = UMCUB_CFG_USB_PID,
    .bcdDevice = 0x0101,                        /* 1.01: Windows asks again for the MS OS 2.0 descriptors */
    .iManufacturer = STR_MANUFACTURER,
    .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIAL,
    .bNumConfigurations = 1,
};

static const uint8_t config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_LEN, 0x80, 100),
#if CFG_TUD_CDC
    TUD_CDC_DESCRIPTOR(ITF_CDC_CTRL, STR_CDC, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 64),
#endif
#if CFG_TUD_DFU
    TUD_DFU_DESCRIPTOR(ITF_DFU, UMCUB_CFG_IMAGE_NUMBER, STR_DFU_ALT0,
                       DFU_ATTR_CAN_DOWNLOAD | DFU_ATTR_MANIFESTATION_TOLERANT | DFU_ATTR_WILL_DETACH |
                           (UMCUB_CFG_READBACK ? DFU_ATTR_CAN_UPLOAD : 0),
                       1000, CFG_TUD_DFU_XFER_BUFSIZE),
#endif
};

#if CFG_TUD_DFU
/* MS OS 2.0 descriptors: Windows binds WinUSB to the DFU interface by itself, so dfu-util (libusb) works there
 * without a driver installed by hand. */
#define VENDOR_REQUEST_MS 1
#define MS_OS_20_REG_LEN  (10 + 42 + 80)    /* the DeviceInterfaceGUIDs property: name and value, UTF-16 */
#define MS_OS_20_LEN      (10 + CFG_TUD_CDC * (8 + 8) + 20 + MS_OS_20_REG_LEN)

static const uint8_t ms_os_20[] = {
    U16_TO_U8S_LE(10), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR), U32_TO_U8S_LE(0x06030000),
    U16_TO_U8S_LE(MS_OS_20_LEN),
#if CFG_TUD_CDC     /* a composite device: the feature belongs to the DFU function */
    U16_TO_U8S_LE(8), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION), 0, 0, U16_TO_U8S_LE(MS_OS_20_LEN - 10),
    U16_TO_U8S_LE(8), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION), ITF_DFU, 0,
    U16_TO_U8S_LE(8 + 20 + MS_OS_20_REG_LEN),
#endif
    U16_TO_U8S_LE(20), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID), 'W', 'I', 'N', 'U', 'S', 'B', 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    /* libusb (dfu-util) finds a WinUSB function of a composite device by this interface GUID (fixed for umcub) */
    U16_TO_U8S_LE(MS_OS_20_REG_LEN), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY), U16_TO_U8S_LE(7 /* REG_MULTI_SZ */),
    U16_TO_U8S_LE(42), 'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0, 'I', 0, 'n', 0, 't', 0, 'e', 0, 'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0, 'G', 0, 'U', 0, 'I', 0, 'D', 0, 's', 0, 0, 0,
    U16_TO_U8S_LE(80), '{', 0, 'D', 0, 'B', 0, '7', 0, '0', 0, '2', 0, 'E', 0, 'E', 0, 'D', 0, '-', 0, 'F', 0, '5', 0, 'A', 0, 'E', 0, '-', 0, '4', 0, '3', 0, '0', 0, '2', 0, '-', 0, '8', 0, '5', 0, '8', 0, '1', 0, '-', 0, 'F', 0, '6', 0, 'D', 0, 'A', 0, '0', 0, '8', 0, 'A', 0, '0', 0, 'A', 0, '8', 0, '5', 0, '7', 0, '}', 0, 0, 0, 0, 0,
};
_Static_assert(sizeof(ms_os_20) == MS_OS_20_LEN, "MS OS 2.0 descriptor set length");

static const uint8_t bos_desc[] = {
    TUD_BOS_DESCRIPTOR(TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_LEN, VENDOR_REQUEST_MS),
};

uint8_t const *tud_descriptor_bos_cb(void)
{
    return bos_desc;
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }
    if (request->bRequest == VENDOR_REQUEST_MS && request->wIndex == 7) {     /* MS_OS_20_DESCRIPTOR_INDEX */
        return tud_control_xfer(rhport, request, (void *)(uintptr_t)ms_os_20, sizeof(ms_os_20));
    }
    return false;
}
#endif

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&device_desc;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return config_desc;
}

static const char *const strings[] = {
    [STR_MANUFACTURER] = UMCUB_CFG_USB_MANUFACTURER,
    [STR_PRODUCT] = UMCUB_CFG_USB_PRODUCT,
    [STR_CDC] = "umcub SMP",
    [STR_DFU_ALT0] = "image 0 (secondary slot)",
    [STR_DFU_ALT0 + 1] = "image 1 (secondary slot)",
};

static uint16_t str_buf[33];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    size_t n;
    if (index == STR_LANG) {
        str_buf[1] = 0x0409;
        n = 1;
    } else if (index == STR_SERIAL) {
        uint8_t uid[12];
        umcub_port_uid(uid);
        n = 24;
        for (size_t i = 0; i < 12; i++) {
            str_buf[1 + 2 * i] = "0123456789ABCDEF"[uid[i] >> 4];
            str_buf[2 + 2 * i] = "0123456789ABCDEF"[uid[i] & 15];
        }
    } else if (index < sizeof(strings) / sizeof(strings[0]) && strings[index]) {
        const char *s = strings[index];
        n = strlen(s);
        if (n > 32) {
            n = 32;
        }
        for (size_t i = 0; i < n; i++) {
            str_buf[1 + i] = (uint8_t)s[i];
        }
    } else {
        return NULL;
    }
    str_buf[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return str_buf;
}
