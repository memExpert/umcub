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

#endif
