/*
 * Host-OS calls of MCUboot's boot_serial (mynewt-style APIs).
 */
#include <stdint.h>
#include "umcub_port.h"
#include "umcub_proto.h"
#include "os/os_cputime.h"
#include "hal/hal_system.h"

/* Used by boot_serial only right before hal_system_reset(): let the reset
 * response leave. */
void os_cputime_delay_usecs(uint32_t usecs)
{
    umcub_mux_flush((usecs + 999u) / 1000u);
}

void hal_system_reset(void)
{
    umcub_port_reset();
}
