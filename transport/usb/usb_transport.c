/*
 * USB device transport (tinyUSB): CDC-ACM carries SMP serial framing,
 * DFU support lives in usb_dfu.c.
 */
#include "tusb.h"
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "umcub_port_usb.h"
#include "umcub_transport.h"
#include "umcub_handoff.h"
#include "umcub_log.h"

static int rhport = -1;

void umcub_usb_irq_handler(void)
{
    tud_int_handler((uint8_t)rhport);
}

static int usb_init(void)
{
    if (rhport >= 0) {
        return 0;
    }
    int rc = umcub_port_usb_init();
    if (rc < 0) {
        UMCUB_LOG_ERR("usb: init failed %d", rc);
        return rc;
    }
    rhport = rc;
    tusb_rhport_init_t dev_init = { .role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO };
    if (!tusb_init((uint8_t)rhport, &dev_init)) {
        UMCUB_LOG_ERR("usb: tusb_init failed");
        return UMCUB_EIO;
    }
    UMCUB_LOG_INF("usb: device on port %d", rhport);
    return 0;
}

static void usb_deinit(void)
{
    if (rhport < 0) {
        return;
    }
    tud_disconnect();
    umcub_port_delay_ms(5);
    tusb_deinit((uint8_t)rhport);
    umcub_port_usb_deinit();
    rhport = -1;
}

void umcub_usb_dfu_poll(void);

static void usb_poll(void)
{
    if (rhport >= 0) {
        tud_task();
#if CFG_TUD_DFU
        umcub_usb_dfu_poll();
#endif
    }
}

#if CFG_TUD_CDC
static size_t cdc_read(uint8_t *buf, size_t max)
{
    if (rhport < 0 || !tud_cdc_available()) {
        return 0;
    }
    return tud_cdc_read(buf, (uint32_t)max);
}

static void cdc_write(const uint8_t *buf, size_t len)
{
    if (rhport < 0 || !tud_cdc_connected()) {
        return;
    }
    uint32_t start = umcub_port_millis();
    while (len) {
        uint32_t n = tud_cdc_write(buf, (uint32_t)len);
        buf += n;
        len -= n;
        if (len) {
            tud_cdc_write_flush();
            tud_task();
            if ((uint32_t)(umcub_port_millis() - start) > 500u) {
                return;     /* host stopped reading */
            }
        }
    }
    tud_cdc_write_flush();
}
#endif

const umcub_transport_t umcub_transport_usb = {
    .id = CFG_TUD_CDC ? UMCUB_TRANSPORT_USB_CDC : UMCUB_TRANSPORT_USB_DFU,
    .name = "usb",
    .init = usb_init,
    .deinit = usb_deinit,
    .poll = usb_poll,
#if CFG_TUD_CDC
    .read = cdc_read,
    .write = cdc_write,
#endif
};
