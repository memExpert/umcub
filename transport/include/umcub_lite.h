/*
 * Recovery frames next to SMP (transport/mux.c):
 *  - lite upload protocol (UMCUB_CFG_LITE_UPLOAD, transport/lite.c);
 *  - the board's own protocol (UMCUB_CFG_PROTO_USER, umcub_proto_user()).
 *
 * Packet transports (CAN ISO-TP, UDP, board packet transports) and umcub link
 * DATA payloads carry a frame as one packet. Plain stream transports (UART,
 * USB CDC) carry it as a line 0x05 <kind> <base64 of the frame> '\n'.
 */
#ifndef UMCUB_LITE_H
#define UMCUB_LITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "umcub_transport.h"

#define UMCUB_FRAME_LINE_START  0x05
#define UMCUB_FRAME_LITE        0x0C    /* line kind: lite upload frame */
#define UMCUB_FRAME_USER        0x0D    /* line kind: board protocol frame */

/* Lite upload frames (little endian; last two bytes: CRC-16/XMODEM of the
 * rest, big endian):
 *   host -> device  A6 'B' image:u8 slot:u8 size:u32   begin; slot 0 primary, 1 secondary
 *                   A6 'D' off:u32 data[...]           data at `off` (continues the image)
 *                   A6 'E' flags:u8                    end; bit 0 mark for a test boot,
 *                                                      bit 1 permanent (secondary slot)
 *                   A6 'R'                             reset
 *   device -> host  A6 'k' rc:i8 off:u32               answer to each frame; off = bytes taken
 * rc is UMCUB_OK or a negative UMCUB_E* code. Host side: tools/umcub_lite.py. */
#define UMCUB_LITE_MAGIC        0xA6

/* A frame that starts with UMCUB_LITE_MAGIC: handled (answered) and true;
 * anything else false. */
bool umcub_lite_rx(const umcub_transport_t *t, const uint8_t *frame, size_t len);

/* One frame to `t`: a packet, or a line of `kind` on a plain stream. */
void umcub_mux_send_frame(const umcub_transport_t *t, uint8_t kind, const uint8_t *frame, size_t len);

#endif /* UMCUB_LITE_H */
