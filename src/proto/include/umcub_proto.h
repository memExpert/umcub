/*
 * Protocol layer API (src/proto/mux.c): the recovery loop the boot core runs,
 * and the board protocol hook.
 */
#ifndef UMCUB_PROTO_H
#define UMCUB_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "umcub_transport.h"

/* UMCUB_CFG_PROTO_USER: implemented by the board (boards/<b>/umcub_board.c).
 * A frame that is neither SMP, a lite upload frame nor a text command: a
 * packet of a packet transport, a umcub link DATA payload, or the decoded
 * line "0x05 0x0D <base64> \n" of a plain stream transport. Return true if
 * it was yours (the bootloader then stays in recovery during the entry
 * window). Write images with umcub_slot_* (umcub.h). */
bool umcub_proto_user(const umcub_transport_t *t, const uint8_t *data, size_t len);
/* Answer `t` with one frame (a packet, or a 0x05 0x0D line on a plain stream). */
void umcub_proto_reply(const umcub_transport_t *t, const uint8_t *data, size_t len);

/* Before a reset from inside a request (SMP, text command, lite): ends the
 * request and polls the transports for `ms` so the answer leaves (USB,
 * Ethernet need polling). */
void umcub_mux_flush(uint32_t ms);

/* Poll for up to `ms`; true as soon as a host started talking SMP (the
 * request is kept and served by umcub_recovery_run()). */
bool umcub_recovery_wait(uint32_t ms);
/* Serve SMP until the host resets the device. Never returns. */
__attribute__((noreturn)) void umcub_recovery_run(void);

/* Transport that last delivered an SMP request (UMCUB_TRANSPORT_*). */
uint8_t umcub_recovery_last_transport(void);

#endif /* UMCUB_PROTO_H */
