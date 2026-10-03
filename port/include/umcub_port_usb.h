/*
 * Port capability: USB device controller board-level setup for tinyUSB
 * (clocks, PHY power, pins, interrupt). The stack itself lives in the
 * transport.
 */
#ifndef UMCUB_PORT_USB_H
#define UMCUB_PORT_USB_H

#include <stdint.h>

/* Returns the tinyUSB root-hub port number to use. */
int umcub_port_usb_init(void);
void umcub_port_usb_deinit(void);

/* Implemented by the USB transport, called from the USB interrupt. */
void umcub_usb_irq_handler(void);

#endif
