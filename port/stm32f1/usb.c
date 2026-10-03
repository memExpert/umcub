/*
 * STM32F1 port: USB full-speed device (USB FS, PA11/PA12) for tinyUSB
 * (stm32_fsdev, rhport 0). RM0008 §23.
 *
 *  - 48 MHz USB clock = PLL 72 MHz / 1.5 (USBPRE = 0): needs the HSE PLL.
 *  - The pins belong to the USB transceiver as soon as the peripheral is
 *    enabled; no GPIO setup.
 *  - F1 has no internal D+ pull-up: boards like the Blue Pill tie D+ to 3.3 V
 *    through a resistor, so the device looks attached whenever it is powered,
 *    even with the USB peripheral off, and a host would try to enumerate a
 *    silent device (some host controllers handle that badly). So D+ is
 *    held low (SE0 = detached) whenever the bootloader's USB is not running: briefly before init, and from deinit on - also into
 *    the application. An application that uses USB releases PA12 (input)
 *    before it enables its USB peripheral.
 */
#include "f1.h"
#include "umcub_port_usb.h"

#define USB_DP          UMCUB_PIN('A', 12, 0)
#define DETACH_MS       15u     /* host debounce for a detach is ~2.5 us, 15 ms is generous */

void f1_usb_hold_detached(void)
{
    GPIO_TypeDef *g = f1_gpio_port(USB_DP);
    g->BRR = 1u << 12;
    f1_gpio_config(USB_DP, 0x2u);           /* output push-pull 2 MHz, low: overrides the pull-up */
}

int umcub_port_usb_init(void)
{
    if (f1_sysclk_hz != 72000000u) {
        return UMCUB_ENOTSUP;               /* no 48 MHz without the 72 MHz HSE PLL */
    }
    f1_usb_hold_detached();                        /* host sees a fresh attach below */
    umcub_port_delay_ms(DETACH_MS);
    f1_gpio_config(USB_DP, F1_GPIO_IN_FLOAT);
    CLEAR_BIT(RCC->CFGR, RCC_CFGR_USBPRE);  /* PLL / 1.5 */
    SET_BIT(RCC->APB1ENR, RCC_APB1ENR_USBEN);
    (void)RCC->APB1ENR;
    f1_periph_used(&RCC->APB1RSTR, RCC_APB1RSTR_USBRST);
    NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 3);
    NVIC_SetPriority(USB_HP_CAN1_TX_IRQn, 3);
    return 0;   /* tinyUSB rhport 0; dcd_int_enable() enables the IRQs */
}

void umcub_port_usb_deinit(void)
{
    NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
    NVIC_DisableIRQ(USB_HP_CAN1_TX_IRQn);
    NVIC_ClearPendingIRQ(USB_LP_CAN1_RX0_IRQn);
    NVIC_ClearPendingIRQ(USB_HP_CAN1_TX_IRQn);
    USB->CNTR = USB_CNTR_FRES | USB_CNTR_PDWN;   /* transceiver off, pins released */
    f1_usb_hold_detached();    /* stays low: no silent "attached" device during boot_go and in the application */
}

void USB_LP_CAN1_RX0_IRQHandler(void)
{
    umcub_usb_irq_handler();
}

void USB_HP_CAN1_TX_IRQHandler(void)
{
    umcub_usb_irq_handler();
}
