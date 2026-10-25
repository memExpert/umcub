/*
 * Boot state shared by the layers (src/util/state.c): handoff requests from the
 * application, last transport, node address on shared buses.
 */
#ifndef UMCUB_STATE_H
#define UMCUB_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include "umcub_cfg.h"
#include "umcub_handoff.h"

uint32_t umcub_handoff_take_request(uint32_t *arg);   /* UMCUB_REQ_* */
void umcub_handoff_request(uint32_t request, uint32_t arg);
void umcub_handoff_publish(const umcub_handoff_t *info);
void umcub_handoff_note_transport(uint8_t id);
uint8_t umcub_handoff_last_transport(void);
uint32_t umcub_handoff_boot_count(void);
/* Node address on a shared bus: application request -> UMCUB_CFG_NODE_ADDR_FLASH
 * -> board hook -> 0. Only umcub link and the CAN IDs use one; other builds
 * have none (always 0). */
#if UMCUB_CFG_LINK_ANY || UMCUB_CFG_TRANSPORT_CAN
bool umcub_handoff_node_request(uint16_t *addr);
void umcub_node_init(void);
uint16_t umcub_node_address(void);
bool umcub_board_node_address(uint16_t *addr);   /* weak, override in umcub_board.c */
#else
static inline void umcub_node_init(void) {}
static inline uint16_t umcub_node_address(void) { return 0; }
#endif

#endif
