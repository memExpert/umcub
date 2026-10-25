/*
 * umcub example application, CM7 of NUCLEO-H755ZI-Q: the console of
 * examples/common/umcub_example.h on the ST-LINK VCP (USART3, 115200);
 * LED LD1.
 */
#include "stm32h7xx.h"
#include "h7_example.h"
#include "umcub_example.h"

static volatile uint32_t ms;
void SysTick_Handler(void) { ms++; }
uint32_t ex_millis(void) { return ms; }

void ex_led_toggle(void) { h7_led_toggle('B', 0); }
void ex_watchdog_feed(void) { IWDG1->KR = 0xAAAAu; }

int main(void)
{
    SysTick_Config(SystemCoreClock / 1000u);
    h7_led_init('B', 0);
    h7_uart_init();
    ex_run("CM7");
}
