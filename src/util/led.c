/*
 * Status LED patterns, see umcub_led.h.
 */
#include "umcub_led.h"
#include "umcub_port.h"

static bool started;
static uint8_t error;
static uint32_t activity_at;

void umcub_led_poll(void)
{
    uint32_t t = umcub_port_millis();
    if (!started) {
        started = true;
        activity_at = t - 1000u;
        umcub_port_gpio_output(UMCUB_CFG_STATUS_LED_PIN, !UMCUB_CFG_STATUS_LED_ACTIVE);
    }
    bool on;
    if (t - activity_at < 100u) {
        on = (t / 50u) & 1u;                            /* 10 Hz while data arrives */
    } else if (error) {
        uint32_t p = t % (error * 400u + 1200u);        /* N x (200 on, 200 off), 1.2 s pause */
        on = p < error * 400u && p % 400u < 200u;
    } else {
        on = !((t / 500u) & 1u);                        /* 1 Hz, on first */
    }
    umcub_port_gpio_write(UMCUB_CFG_STATUS_LED_PIN, on == (UMCUB_CFG_STATUS_LED_ACTIVE != 0));
}

void umcub_led_activity(void)
{
    activity_at = umcub_port_millis();
}

void umcub_led_error(unsigned code)
{
    if (!error) {
        error = (uint8_t)code;
    }
}

void umcub_led_off(void)
{
    if (started) {
        started = false;
        umcub_port_gpio_reset(UMCUB_CFG_STATUS_LED_PIN);
    }
}
