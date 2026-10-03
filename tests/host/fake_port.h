#ifndef FAKE_PORT_H
#define FAKE_PORT_H
#include <setjmp.h>
#include <stdint.h>
extern uint8_t fake_flash[];
extern jmp_buf *fake_reset_jmp;
extern unsigned fake_flash_erase_count;
extern int fake_realtime;            /* umcub_port_millis() from CLOCK_MONOTONIC */
extern uint32_t fake_uid_seed;       /* varies umcub_port_uid() between simulated devices */
extern int fake_rdp_level;
extern int fake_entropy_fail;        /* umcub_port_entropy() fails */          /* umcub_port_rdp_level(), default 1 */
void fake_flash_reset(void);
#endif
