/*
 * umcub link: addressing (and, in SECURE mode, authentication) on top of every
 * transport, for several devices on one bus (RS485, CAN, UDP). A transport
 * with .link != UMCUB_LINK_PLAIN accepts only umcub link frames; everything
 * else (plain NLIP, raw SMP, typed text) is ignored.
 *
 * Frame (little endian):
 *   0 magic 0xA5 | 1 version | 2 type | 3 flags | 4 dst u16 | 6 src u16 |
 *   8 seq u32 | 12 len u16 | 14 payload[len] | [tag 16 B if FLAG_MAC]
 * Packet transports (CAN ISO-TP, UDP, board packet transports) carry the frame
 * as is; stream transports (UART, USB CDC) as one line: 0x05 0x0B, base64 of
 * the frame, '\n'.
 *
 * Addresses: the node address (umcub_node_address(), 0 = unassigned),
 * 0xFFFF = broadcast. DATA payloads are raw SMP packets or text commands; the
 * answer comes back as DATA to the sender.
 */
#ifndef UMCUB_LINK_H
#define UMCUB_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "umcub_cfg.h"
#include "umcub_transport.h"

#define UMCUB_LINK_MAGIC        0xA5u
#define UMCUB_LINK_VERSION      1u
#define UMCUB_LINK_HDR          14u
#define UMCUB_LINK_TAG          16u
#define UMCUB_LINK_BROADCAST    0xFFFFu
#define UMCUB_LINK_LINE_START1  0x05
#define UMCUB_LINK_LINE_START2  0x0B

enum {
    UMCUB_LT_DISCOVER = 1,      /* host, broadcast: u8 slots, u16 slot_ms, n x UID[12] already found */
    UMCUB_LT_ANNOUNCE = 2,      /* device: identity (see link.c) */
    UMCUB_LT_HELLO = 3,         /* host: select dst (or, FLAG_UID, the device with UID[12]) */
    UMCUB_LT_CHALLENGE = 4,     /* SECURE */
    UMCUB_LT_AUTH = 5,          /* SECURE */
    UMCUB_LT_AUTH_OK = 6,       /* SECURE */
    UMCUB_LT_DATA = 7,          /* SMP packet or text command, both directions */
    UMCUB_LT_CLOSE = 8,         /* host: end of session */
};
#define UMCUB_LF_MAC            0x01u
#define UMCUB_LF_ENC            0x02u
#define UMCUB_LF_UID            0x04u

/* Largest frame and its line form (stream transports). */
#define UMCUB_LINK_FRAME_MAX    (UMCUB_LINK_HDR + UMCUB_CFG_SMP_MTU + UMCUB_LINK_TAG)
#define UMCUB_LINK_LINE_MAX     (2 + ((UMCUB_LINK_FRAME_MAX + 2) / 3) * 4 + 1)

/* From the transports / mux: one complete frame, or one line without the two
 * start bytes and the newline. */
void umcub_link_rx(const umcub_transport_t *t, const uint8_t *frame, size_t len);
void umcub_link_rx_line(const umcub_transport_t *t, const char *b64, size_t len);
/* Answer to the peer of the last request on `t` (DATA). */
void umcub_link_send_data(const umcub_transport_t *t, const uint8_t *data, size_t len);
/* Timed work (delayed ANNOUNCE after DISCOVER); call from the main loop. */
void umcub_link_poll(void);
/* True once after a frame addressed to this node (HELLO, DATA) arrived: keeps
 * the bootloader during UMCUB_CFG_ENTRY_WAIT_MS like a "stay" command. */
bool umcub_link_take_wakeup(void);

/* Provided by the mux: queue a DATA payload of `t` (SMP or text). */
bool umcub_mux_link_rx(const umcub_transport_t *t, const uint8_t *payload, size_t len);

#endif /* UMCUB_LINK_H */
