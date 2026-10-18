/*
 * STM32H7 port: FDCAN1 for the FDCAN driver (port/arch/stm32/fdcan.c,
 * RM0399 §56), kernel clock PLL1Q.
 */
#include "h7.h"
#include "../arch/stm32/stm32_fdcan.h"

static const stm32_fdcan_t fdcan1 = { FDCAN1, &RCC->APB1HENR, &RCC->APB1HRSTR, RCC_APB1HENR_FDCANEN, &h7_pll1q_hz };

const stm32_fdcan_t *stm32_fdcan_get(const umcub_can_cfg_t *cfg)
{
    if (cfg->instance != 1) {
        return NULL;    /* FDCAN2 shares the message RAM: not wired up yet */
    }
    MODIFY_REG(RCC->D2CCIP1R, RCC_D2CCIP1R_FDCANSEL, 1u << RCC_D2CCIP1R_FDCANSEL_Pos);   /* PLL1Q */
    umcub_port_gpio_af(cfg->tx_pin);
    umcub_port_gpio_af(cfg->rx_pin);
    return &fdcan1;
}
