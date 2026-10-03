/*
 * Bootloader-internal API of the boot core.
 */
#ifndef UMCUB_BOOT_H
#define UMCUB_BOOT_H

#include <stdbool.h>
#include <stdint.h>
#include "umcub_handoff.h"

uint32_t umcub_handoff_take_request(uint32_t *arg);   /* UMCUB_REQ_* */
void umcub_handoff_request(uint32_t request, uint32_t arg);
void umcub_handoff_publish(const umcub_handoff_t *info);
void umcub_handoff_note_transport(uint8_t id);
uint8_t umcub_handoff_last_transport(void);
bool umcub_handoff_node_request(uint16_t *addr);
/* Node address on a shared bus: application request -> board hook -> 0. */
void umcub_node_init(void);
uint16_t umcub_node_address(void);
bool umcub_board_node_address(uint16_t *addr);   /* weak, override in umcub_board.c */

#endif
