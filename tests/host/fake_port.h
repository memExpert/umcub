#ifndef FAKE_PORT_H
#define FAKE_PORT_H
#include <setjmp.h>
#include <stdint.h>
extern uint8_t fake_flash[];
extern jmp_buf *fake_reset_jmp;
extern unsigned fake_flash_erase_count;
void fake_flash_reset(void);
#endif
