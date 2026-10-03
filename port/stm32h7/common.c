/*
 * STM32H7 port: helpers shared by the bootloader and the application
 * library (flash driver dependencies only - no SysTick, no clocks).
 */
#include "h7.h"

int h7_wait(volatile uint32_t *reg, uint32_t mask, uint32_t value, uint32_t timeout_ms)
{
    uint32_t start = umcub_port_millis();
    while ((*reg & mask) != value) {
        if ((uint32_t)(umcub_port_millis() - start) > timeout_ms) {
            return UMCUB_ETIMEOUT;
        }
    }
    return UMCUB_OK;
}

uint32_t umcub_port_irq_save(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

void umcub_port_irq_restore(uint32_t state)
{
    __set_PRIMASK(state);
}

