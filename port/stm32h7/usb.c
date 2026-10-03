/*
 * STM32H7 port: USB OTG FS (USB2_OTG_FS, PA11/PA12) for tinyUSB rhport 0.
 * Kernel clock HSI48 trimmed by CRS on USB SOF (RM0399 §8, §9 CRS).
 */
#include "h7.h"
#include "umcub_port_usb.h"
#include "stm32h7xx_ll_crs.h"

#define USB_DM UMCUB_PIN('A', 11, 10)
#define USB_DP UMCUB_PIN('A', 12, 10)

int umcub_port_usb_init(void)
{
    LL_RCC_HSI48_Enable();
    if (h7_wait(&RCC->CR, RCC_CR_HSI48RDY, RCC_CR_HSI48RDY, 10) != 0) {
        return UMCUB_EIO;
    }
    LL_RCC_SetUSBClockSource(LL_RCC_USB_CLKSOURCE_HSI48);

    SET_BIT(RCC->APB1HENR, RCC_APB1HENR_CRSEN);
    (void)RCC->APB1HENR;
    h7_periph_used(&RCC->APB1HRSTR, RCC_APB1HRSTR_CRSRST);
    LL_CRS_SetSyncSignalSource(LL_CRS_SYNC_SOURCE_USB);
    LL_CRS_EnableAutoTrimming();
    LL_CRS_EnableFreqErrorCounter();

    LL_PWR_EnableUSBVoltageDetector();

    umcub_port_gpio_af(USB_DM);
    umcub_port_gpio_af(USB_DP);

    SET_BIT(RCC->AHB1ENR, RCC_AHB1ENR_USB2OTGFSEN);
    (void)RCC->AHB1ENR;
    /* No external ULPI on this controller: keep its clock off in sleep. */
    CLEAR_BIT(RCC->AHB1LPENR, RCC_AHB1LPENR_USB2OTGFSULPILPEN);
    h7_periph_used(&RCC->AHB1RSTR, RCC_AHB1RSTR_USB2OTGFSRST);

    NVIC_SetPriority(OTG_FS_IRQn, 3);
    return 0;   /* tinyUSB rhport 0 = OTG_FS */
}

void umcub_port_usb_deinit(void)
{
    NVIC_DisableIRQ(OTG_FS_IRQn);
    NVIC_ClearPendingIRQ(OTG_FS_IRQn);
    LL_PWR_DisableUSBVoltageDetector();
    umcub_port_gpio_reset(USB_DM);
    umcub_port_gpio_reset(USB_DP);
}

void OTG_FS_IRQHandler(void)
{
    umcub_usb_irq_handler();
}
