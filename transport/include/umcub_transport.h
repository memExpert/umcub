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
} umcub_transport_t;

/* Registry (transport/registry.c): every transport enabled in umcub_config.h. */
extern const umcub_transport_t *const umcub_transports[];
extern const unsigned umcub_transport_count;

/* Called by packet transports when a complete SMP packet arrived. The
 * buffer must stay valid until the call returns; returns false if the mux is
 * busy (packet dropped, host will retry). */
bool umcub_smp_packet_rx(const umcub_transport_t *t, const uint8_t *pkt, size_t len);

/* Lifecycle used by the boot core. */
void umcub_transports_init(void);
void umcub_transports_deinit(void);
void umcub_transports_poll(void);

/* Poll for up to `ms`; true as soon as a host started talking SMP (the
 * request is kept and served by umcub_recovery_run()). */
bool umcub_recovery_wait(uint32_t ms);
/* Serve SMP until the host resets the device. Never returns. */
__attribute__((noreturn)) void umcub_recovery_run(void);

/* Transport that last delivered an SMP request (UMCUB_TRANSPORT_*). */
uint8_t umcub_recovery_last_transport(void);

#endif /* UMCUB_TRANSPORT_H */
