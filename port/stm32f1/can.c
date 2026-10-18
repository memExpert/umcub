/*
 * STM32F1 port: CAN1 for the bxCAN driver (port/arch/stm32/bxcan.c, RM0008
 * §24). Pins by the TX pin: PA12/PA11 (default), PB9/PB8 (AFIO CAN_REMAP =
 * 10), PD1/PD0 (CAN_REMAP = 11). On F103 parts CAN and USB share 512 bytes
 * of SRAM: they cannot be used together (config check).
 */
#include "f1.h"
#include "../arch/stm32/stm32_can.h"

#define PIN(port, num) UMCUB_PIN(port, num, 0)

/* TX, RX, AFIO CAN_REMAP: the table below and the build-time check */
#define MAPS(X)                                                                                         \
    X(PIN('A', 12), PIN('A', 11), 0)                                                                    \
    X(PIN('B', 9), PIN('B', 8), AFIO_MAPR_CAN_REMAP_REMAP2)                                             \
    X(PIN('D', 1), PIN('D', 0), AFIO_MAPR_CAN_REMAP_REMAP3)     /* 100/144-pin packages only */

#define CONFIGURED(tx, rx, remap) (UMCUB_CFG_CAN_TX_PIN == (tx) && UMCUB_CFG_CAN_RX_PIN == (rx)) ||
_Static_assert(UMCUB_CFG_CAN_INSTANCE == 1 && (MAPS(CONFIGURED) 0),
               "umcub: UMCUB_CFG_CAN_TX_PIN / _RX_PIN are not CAN1 pins (STM32F1: PA12/PA11, PB9/PB8 or PD1/PD0, "
               "AF 0; UMCUB_CFG_CAN_INSTANCE 1)");

#define ENTRY(tx, rx, remap) { tx, rx, remap },
static const struct {
    uint32_t tx, rx, remap;
} map[] = { MAPS(ENTRY) };

static const stm32_bxcan_t can1 = { CAN1, &RCC->APB1ENR, &RCC->APB1RSTR, RCC_APB1ENR_CAN1EN, &f1_pclk1_hz };

const stm32_bxcan_t *stm32_bxcan_get(const umcub_can_cfg_t *cfg)
{
    for (unsigned m = 0; cfg->instance == 1 && m < sizeof(map) / sizeof(map[0]); m++) {
        if (map[m].tx != cfg->tx_pin || map[m].rx != cfg->rx_pin) {
            continue;
        }
        if (map[m].remap) {
            umcub_cm_periph_on(&RCC->APB2ENR, &RCC->APB2RSTR, RCC_APB2ENR_AFIOEN);
            /* SWJ_CFG reads back undefined: write 111 ("no effect") with the remap
             * bits, or SWD may get switched off (see uart.c). */
            MODIFY_REG(AFIO->MAPR, AFIO_MAPR_CAN_REMAP | AFIO_MAPR_SWJ_CFG, map[m].remap | AFIO_MAPR_SWJ_CFG);
        }
        umcub_port_gpio_af(cfg->tx_pin);
        umcub_port_gpio_input(cfg->rx_pin, UMCUB_PULL_UP);   /* recessive while no transceiver drives it */
        return &can1;
    }
    return NULL;
}
