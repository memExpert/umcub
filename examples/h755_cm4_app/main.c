/*
 * umcub example application, CM4 of NUCLEO-H755ZI-Q (image 1 of the CM7
 * bootloader in UMCUB_DUALCORE_SINGLE_BOOT). Blinks LD2 and confirms itself
 * after 5 seconds of healthy operation.
 */
#include "stm32h7xx.h"
#include "h7_example.h"
#include "umcub.h"

int main(void)
{
    ex_led_init('E', 1);
    for (uint32_t t = 0;; t++) {
        ex_led_toggle('E', 1);
        ex_delay_ms(100);
        if (t == 50) {
            (void)umcub_confirm();
        }
    }
}
