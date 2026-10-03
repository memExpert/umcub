#ifndef FAKE_PORT_H
#define FAKE_PORT_H
#include <setjmp.h>
#include <stdint.h>
extern uint8_t fake_flash[];
extern jmp_buf *fake_reset_jmp;
extern unsigned fake_flash_erase_count;
extern int fake_rdp_level;          /* umcub_port_rdp_level(), default 1 */
void fake_flash_reset(void);
#endif
