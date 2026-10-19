/*
 * Helpers of the umcub link host tests, see link_test.h.
 */
#include <string.h>

#include "link_test.h"
#include "umcub_cfg.h"
#include "umcub_link.h"
#include "boot_serial/boot_serial.h"
#include "base64/base64.h"

extern const struct boot_uart_funcs *boot_uf;

int failures;

size_t stream_read(stream_t *s, uint8_t *b, size_t m)
{
    size_t n = 0;
    while (n < m && s->in_pos < s->in_len) {
        b[n++] = s->in[s->in_pos++];
    }
    return n;
}

void stream_write(stream_t *s, const uint8_t *b, size_t l)
{
    if (s->out_len + l < sizeof(s->out)) {
        memcpy(&s->out[s->out_len], b, l);
        s->out_len += l;
    }
}

void stream_reset(stream_t *s)
{
    s->in_len = s->in_pos = s->out_len = 0;
}

void stream_put_frame(stream_t *s, const uint8_t *f, size_t n)
{
    s->in[s->in_len++] = UMCUB_LINK_LINE_START1;
    s->in[s->in_len++] = UMCUB_LINK_LINE_START2;
    s->in_len += (size_t)base64_encode(f, (int)n, (char *)&s->in[s->in_len], 1);
    s->in[s->in_len++] = '\n';
}

size_t stream_out_frame(const stream_t *s, int k, uint8_t *f)
{
    const char *p = s->out, *end = s->out + s->out_len;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        if (!nl) {
            break;
        }
        if (p[0] == UMCUB_LINK_LINE_START1 && p[1] == UMCUB_LINK_LINE_START2 && k-- == 0) {
            char line[4096];
            size_t l = (size_t)(nl - p - 2);
            memcpy(line, p + 2, l);
            line[l] = 0;
            int n = base64_decode(line, f);
            return n > 0 ? (size_t)n : 0;
        }
        p = nl + 1;
    }
    return 0;
}

uint8_t pkt_out[2048];
size_t pkt_out_len;

int pkt_send(const uint8_t *p, size_t l)
{
    memcpy(pkt_out, p, l);
    pkt_out_len = l;
    return 0;
}

int nop_init(void) { return 0; }
void nop_deinit(void) {}

void run(int n)
{
    char tmp[1500];
    int nl;
    for (int i = 0; i < n; i++) {
        boot_uf->read(tmp, sizeof(tmp), &nl);
    }
}
