/*
 * STM32F4 port: USB OTG FS (PA11/PA12) for tinyUSB rhport 0. The 48 MHz
 * clock is the main PLL's Q output (clock.c: VCO 192 MHz / 4, RM0383 §6.2);
 * USB needs it from the crystal (OTG FS: +-0.25 %), not from the HSI.
 * VBUS sensing stays off (tinyUSB forces B-valid), so PA9 is free.
 */
#include "f4.h"
#include "umcub_port_usb.h"

#define USB_DM UMCUB_PIN('A', 11, 10)
#define USB_DP UMCUB_PIN('A', 12, 10)

int umcub_port_usb_init(void)
{
    if ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL || !(RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC_HSE)) {
        return UMCUB_ENOTSUP;                   /* no HSE: 48 MHz not accurate enough */
    }
    umcub_port_gpio_af(USB_DM);
    umcub_port_gpio_af(USB_DP);
    umcub_cm_periph_on(&RCC->AHB2ENR, &RCC->AHB2RSTR, RCC_AHB2ENR_OTGFSEN);
    NVIC_SetPriority(OTG_FS_IRQn, 3);
    return 0;   /* tinyUSB rhport 0 = OTG_FS */
}

void umcub_port_usb_deinit(void)
{
    NVIC_DisableIRQ(OTG_FS_IRQn);
    NVIC_ClearPendingIRQ(OTG_FS_IRQn);
    umcub_port_gpio_reset(USB_DM);
    umcub_port_gpio_reset(USB_DP);
}

void OTG_FS_IRQHandler(void)
{
    umcub_usb_irq_handler();
}
