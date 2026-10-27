/*
 * Helpers of the umcub link host tests (test_link.c, test_link_secure.c):
 * scripted stream transports, link lines in and out, a captured packet
 * transport, the mux loop.
 */
#ifndef LINK_TEST_H
#define LINK_TEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

extern int failures;
#define CHECK(c)                                                                 \
    do {                                                                         \
        if (!(c)) {                                                              \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                \
            failures++;                                                          \
        }                                                                        \
    } while (0)

/* A stream transport: scripted input, captured output. */
typedef struct {
    uint8_t in[8192];
    size_t in_len, in_pos;
    char out[16384];
    size_t out_len;
} stream_t;

size_t stream_read(stream_t *s, uint8_t *b, size_t m);
void stream_write(stream_t *s, const uint8_t *b, size_t l);
void stream_reset(stream_t *s);
/* Frame f as a line "05 <kind> base64 \n" into the stream's input. */
void stream_put_line(stream_t *s, uint8_t kind, const uint8_t *f, size_t n);
/* The k-th line of that kind the device wrote, decoded into f; returns its length. */
size_t stream_out_line(const stream_t *s, uint8_t kind, int k, uint8_t *f);
/* The same for link lines (kind 0x0B). */
void stream_put_frame(stream_t *s, const uint8_t *f, size_t n);
size_t stream_out_frame(const stream_t *s, int k, uint8_t *f);

/* A packet transport: the last packet sent. */
extern uint8_t pkt_out[2048];
extern size_t pkt_out_len;
int pkt_send(const uint8_t *p, size_t l);
int nop_init(void);
void nop_deinit(void);

/* n passes of the recovery loop (boot_serial's read). */
void run(int n);

#endif /* LINK_TEST_H */
