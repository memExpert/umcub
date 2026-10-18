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
    int (*init)(void);
    void (*deinit)(void);
    void (*poll)(void);                                 /* may be NULL */
    size_t (*read)(uint8_t *buf, size_t max);           /* stream: non-blocking */
    void (*write)(const uint8_t *buf, size_t len);      /* stream: blocking */
    int (*send_packet)(const uint8_t *pkt, size_t len); /* packet transports */
    /* UMCUB_LINK_* (umcub_config_types.h); 0 = plain SMP / text. */
    uint8_t link;
} umcub_transport_t;

/* Upper bound of compiled-in transports (sizes the mux's per-stream buffers). */
#define UMCUB_TRANSPORT_MAX ((UMCUB_CFG_TRANSPORT_UART != 0) + (UMCUB_CFG_USB != 0) + \
                             (UMCUB_CFG_TRANSPORT_CAN != 0) + (UMCUB_CFG_TRANSPORT_ETH != 0) + \
                             (UMCUB_CFG_TRANSPORT_USER != 0))

/* Largest packet a packet transport on a umcub link receives: a link frame
 * (14-byte header + 16-byte tag) around a full SMP packet. Checked against
 * UMCUB_LINK_FRAME_MAX in proto/include/umcub_link.h. */
#define UMCUB_TRANSPORT_LINK_PACKET_MAX (14u + UMCUB_CFG_SMP_MTU + 16u)

/* UMCUB_CFG_TRANSPORT_USER: defined by the board (boards/<b>/umcub_board.c),
 * no default - the build fails to link without it. */
extern const umcub_transport_t umcub_transport_user;

/* Registry (transport/registry.c): every transport enabled in umcub_config.h. */
extern const umcub_transport_t *const umcub_transports[];
extern const unsigned umcub_transport_count;

/* Called by packet transports when a complete packet arrived; implemented by
 * the protocol layer (proto/mux.c). The buffer must stay valid until the call
 * returns; returns false if the mux is busy (packet dropped, host will retry). */
bool umcub_smp_packet_rx(const umcub_transport_t *t, const uint8_t *pkt, size_t len);

/* Lifecycle used by the boot core. */
void umcub_transports_init(void);
void umcub_transports_deinit(void);
void umcub_transports_poll(void);

#endif /* UMCUB_TRANSPORT_H */
