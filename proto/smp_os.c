/*
 * Host-OS calls of MCUboot's boot_serial (mynewt-style APIs).
 */
#include <stdint.h>
#include "umcub_port.h"
#include "umcub_transport.h"
#include "os/os_cputime.h"
#include "hal/hal_system.h"

/* Used by boot_serial before resetting: keep the transports running so the
 * reset response actually leaves (USB, Ethernet need polling). */
void os_cputime_delay_usecs(uint32_t usecs)
{
    uint32_t start = umcub_port_millis();
    while ((uint32_t)(umcub_port_millis() - start) < (usecs + 999u) / 1000u) {
        umcub_transports_poll();
    }
}

void hal_system_reset(void)
{
    umcub_port_reset();
}
