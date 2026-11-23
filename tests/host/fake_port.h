#ifndef FAKE_PORT_H
#define FAKE_PORT_H
#include <setjmp.h>
#include <stdint.h>
extern uint8_t fake_flash[];
extern jmp_buf *fake_reset_jmp;
extern unsigned fake_flash_erase_count;
extern unsigned fake_op_cut;       /* != 0: the power is cut (umcub_port_reset()) after the n-th flash operation from now */
void fake_flash_save(void), fake_flash_restore(void);   /* the flash as it is / back to it */
extern unsigned fake_erase_cut;     /* != 0: the power is cut (umcub_port_reset()) before the erase of the n-th sector from now */
extern uint64_t fake_unfed_us, fake_unfed_us_max;  /* umcub_port_delay_us() time between watchdog feeds */
extern int fake_realtime;            /* umcub_port_millis() from CLOCK_MONOTONIC */
extern uint32_t fake_uid_seed;       /* varies umcub_port_uid() between simulated devices */
extern int fake_rdp_level;           /* umcub_port_rdp_level(), default 1 */
extern uint32_t fake_read_fail_addr, fake_read_fail_len;  /* umcub_flash_read() of these bytes fails (len 0: none) */
extern int fake_entropy_fail;        /* umcub_port_entropy(): 1 fails, 2 stuck, 3 only 6 distinct values */
extern uint32_t fake_gpio_pin;       /* last pin driven / reset, its level, output/reset calls */
extern int fake_gpio_level, fake_gpio_outputs, fake_gpio_resets;
void fake_flash_reset(void);
#endif
