/*
 * umcub transport interface.
 *
 * Every update channel implements one umcub_transport_t:
 *  - stream transports (UART, USB CDC) move SMP serial framing (NLIP lines,
 *    base64) and implement read/write;
 *  - packet transports (CAN ISO-TP, UDP) move raw SMP packets: they hand
 *    received packets to umcub_smp_packet_rx() from poll() and implement
 *    send_packet;
 *  - other transports (USB DFU) only need poll().
 *
 * A board can add its own (UMCUB_CFG_TRANSPORT_USER): define
 *     const umcub_transport_t umcub_transport_user = { .id = UMCUB_TRANSPORT_USER, ... };
 * in boards/<b>/umcub_board.c. Rules for every transport: everything is
 * polled from the bootloader's single loop - poll/read/send_packet must not
 * block (bounded waits only, with a timeout); deinit must put every
 * peripheral, pin, EXTI and DMA channel it touched back into reset state
 * (it runs right before the jump to the application).
 */
#ifndef UMCUB_TRANSPORT_H
#define UMCUB_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct umcub_transport {
    uint8_t id;                                         /* UMCUB_TRANSPORT_* */
    const char *name;
    int (*init)(void);
    void (*deinit)(void);
    void (*poll)(void);                                 /* may be NULL */
    size_t (*read)(uint8_t *buf, size_t max);           /* stream: non-blocking */
    void (*write)(const uint8_t *buf, size_t len);      /* stream: blocking */
    int (*send_packet)(const uint8_t *pkt, size_t len); /* packet transports */
    /* Not started for the UMCUB_CFG_ENTRY_WAIT_MS window, only in recovery
     * mode. USB: a host needs longer than such a window to enumerate and open
     * the device, and a device that vanishes in the middle of enumeration
     * upsets some host controllers. init/deinit/poll/read
     * of such a transport must cope with never having been started. */
    bool skip_entry_window;
    /* UMCUB_LINK_* (umcub_link.h); 0 = plain SMP / text. */
    uint8_t link;
} umcub_transport_t;

/* Upper bound of compiled-in transports (sizes the mux's per-stream buffers). */
#define UMCUB_TRANSPORT_MAX ((UMCUB_CFG_TRANSPORT_UART != 0) + (UMCUB_CFG_USB != 0) + \
                             (UMCUB_CFG_TRANSPORT_CAN != 0) + (UMCUB_CFG_TRANSPORT_ETH != 0) + \
                             (UMCUB_CFG_TRANSPORT_USER != 0))

/* Registry (transport/registry.c): every transport enabled in umcub_config.h. */
extern const umcub_transport_t *const umcub_transports[];
extern const unsigned umcub_transport_count;

/* Called by packet transports when a complete SMP packet arrived. The
 * buffer must stay valid until the call returns; returns false if the mux is
 * busy (packet dropped, host will retry). */
bool umcub_smp_packet_rx(const umcub_transport_t *t, const uint8_t *pkt, size_t len);

/* True while the request being handled came over an encrypted umcub link
 * session (SECURE + UMCUB_CFG_LINK_ENCRYPT): readback of encrypted images. */
bool umcub_mux_request_confidential(void);

/* Lifecycle used by the boot core. */
void umcub_transports_init(void);
/* Only the transports without skip_entry_window (UMCUB_CFG_ENTRY_WAIT_MS). */
void umcub_transports_init_entry_window(void);
void umcub_transports_deinit(void);
void umcub_transports_poll(void);

/* UMCUB_CFG_PROTO_USER: implemented by the board (boards/<b>/umcub_board.c).
 * A frame that is neither SMP, a lite upload frame nor a text command: a
 * packet of a packet transport, a umcub link DATA payload, or the decoded
 * line "0x05 0x0D <base64> \n" of a plain stream transport. Return true if
 * it was yours (the bootloader then stays in recovery during the entry
 * window). Write images with umcub_slot_* (umcub.h). */
bool umcub_proto_user(const umcub_transport_t *t, const uint8_t *data, size_t len);
/* Answer `t` with one frame (a packet, or a 0x05 0x0D line on a plain stream). */
void umcub_proto_reply(const umcub_transport_t *t, const uint8_t *data, size_t len);

/* Poll for up to `ms`; true as soon as a host started talking SMP (the
 * request is kept and served by umcub_recovery_run()). */
bool umcub_recovery_wait(uint32_t ms);
/* Serve SMP until the host resets the device. Never returns. */
__attribute__((noreturn)) void umcub_recovery_run(void);

/* Transport that last delivered an SMP request (UMCUB_TRANSPORT_*). */
uint8_t umcub_recovery_last_transport(void);

#endif /* UMCUB_TRANSPORT_H */
