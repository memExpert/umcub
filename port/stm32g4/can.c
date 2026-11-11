/*
 * STM32G4 port: FDCAN1 for the FDCAN driver (port/arch/stm32/fdcan.c,
 * RM0440 §44), kernel clock PCLK1 (FDCANSEL = 10): the PLL from the HSE when
 * the board has a crystal, so the bit timing is as exact as the crystal.
 * The clock divider of the FDCAN block (FDCAN_CONFIG CKDIV) stays at /1.
 */
#include "g4.h"
#include "stm32g4xx_ll_gpio.h"
#include "../arch/stm32/stm32_fdcan.h"

static const stm32_fdcan_t fdcan1 = { FDCAN1, &RCC->APB1ENR1, &RCC->APB1RSTR1, RCC_APB1ENR1_FDCANEN, &g4_pclk1_hz };

const stm32_fdcan_t *stm32_fdcan_get(const umcub_can_cfg_t *cfg)
{
    if (cfg->instance != 1) {
        return NULL;    /* FDCAN2 / FDCAN3: not wired up yet */
    }
    MODIFY_REG(RCC->CCIPR, RCC_CCIPR_FDCANSEL, 2u << RCC_CCIPR_FDCANSEL_Pos);   /* PCLK1 */
    umcub_port_gpio_af(cfg->tx_pin);
    umcub_port_gpio_af(cfg->rx_pin);
    LL_GPIO_SetPinPull(g4_gpio_port(cfg->rx_pin), 1u << UMCUB_PIN_NUM(cfg->rx_pin), LL_GPIO_PULL_UP);   /* recessive without a transceiver */
    return &fdcan1;
}
