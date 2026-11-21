/*
 * Differential test harness (tests/host/diff_test.py): the firmware's codecs and parsers on cases from stdin, one
 * line each, answers on stdout. Hex in and out.
 *
 *   crc <data>            -> CRC-16/CCITT (src/util/codec.c)
 *   b64e <data>           -> base64 text (padded)
 *   b64d <text>           -> "OK <data>" | "ERR"
 *   link <frame>          -> "1 [<frames sent>]" | "0": an ADDRESSED node (umcub_link_rx) took it
 *   lite <frame>          -> "<answer>" | "-" (umcub_lite_rx on a packet transport)
 *   slot <len>            -> the first len bytes of image 0's primary slot
 *   isotp <fl> <f>,<f>..  -> the messages isotp.c delivers from these CAN frames ("-": none)
 *   isotps <fl> <msg>     -> the CAN frames isotp.c sends for msg (every flow control: clear to send)
 *   img <image>           -> umcub_inspect_verify() of the image in image 0's primary slot (0 = valid)
 * Starts with "cfg mtu=.. node=.. uid=.. board=..".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_port.h"
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "umcub_port_can.h"
#include "umcub_transport.h"
#include "umcub_link.h"
#include "umcub_lite.h"
#include "umcub_inspect.h"
#include "umcub_handoff.h"
#include "isotp.h"
#include "crc/crc16.h"
#include "base64/base64.h"

extern uint16_t fake_node_addr;

#define NODE    5u
#define MAX_IN  (1u << 16)                         /* bytes of one case: a few KiB used */

/* ---- transports: an ADDRESSED packet transport (link), a plain one (lite) ---------------------------------------- */
static char sent[1u << 16];                     /* hex of the packets sent while handling a case, comma separated */
static size_t sent_len;

static void put_hex(char *out, size_t *o, size_t cap, const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n && *o + 3 < cap; i++) {
        *o += (size_t)snprintf(out + *o, cap - *o, "%02x", b[i]);
    }
}

static int capture(const uint8_t *p, size_t l)
{
    if (sent_len && sent_len + 1 < sizeof(sent)) {
        sent[sent_len++] = ',';
    }
    put_hex(sent, &sent_len, sizeof(sent), p, l);
    return 0;
}

static int nop_init(void) { return 0; }
static void nop_deinit(void) {}
static const umcub_transport_t t_link = {
    .id = UMCUB_TRANSPORT_UART, .init = nop_init, .deinit = nop_deinit,
    .send_packet = capture, .link = UMCUB_LINK_ADDRESSED,
};
static const umcub_transport_t t_lite = {
    .id = UMCUB_TRANSPORT_CAN, .init = nop_init, .deinit = nop_deinit, .send_packet = capture,
};
const umcub_transport_t *const umcub_transports[] = { &t_link, &t_lite, 0 };
const unsigned umcub_transport_count = 2;
void umcub_transports_poll(void) {}
void umcub_transports_init(void) {}
void umcub_transports_deinit(void) {}

/* ---- CAN for isotp.c: frames sent are captured, every receive is a clear-to-send flow control ---------------------- */
int umcub_port_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    (void)id;
    return capture(data, len);
}
bool umcub_port_can_tx_idle(void) { return true; }
bool umcub_port_can_recv(uint32_t *id, uint8_t *data, uint8_t *len)
{
    *id = 0;
    data[0] = 0x30;
    data[1] = data[2] = 0;
    *len = 3;
    return true;
}

/* ---- I/O ------------------------------------------------------------------------------------------------------------ */
static uint8_t buf[MAX_IN];
static char out[2 * MAX_IN + 64];

static size_t unhex(const char *s, uint8_t *b, size_t cap)
{
    size_t n = 0;
    unsigned v;
    while (s[0] && s[1] && n < cap && sscanf(s, "%2x", &v) == 1) {
        b[n++] = (uint8_t)v;
        s += 2;
    }
    return n;
}

static void answer_hex(const uint8_t *b, size_t n, const char *prefix)
{
    size_t o = (size_t)snprintf(out, sizeof(out), "%s", prefix);
    put_hex(out, &o, sizeof(out), b, n);
    puts(out);
}

static void slot_clear(void)
{
    memset(&fake_flash[UMCUB_CFG_IMG0_PRIMARY_ADDR - 0x08000000u], 0xFF, UMCUB_CFG_IMG0_PRIMARY_SIZE);
}

/* ---- cases ------------------------------------------------------------------------------------------------------- */
static void isotp_rx(unsigned fl, char *list)
{
    static uint8_t rx[4096];
    isotp_t t = { .frame_len = (uint8_t)fl, .rx_buf = rx, .rx_cap = sizeof(rx) };
    size_t o = 0;
    for (char *f = strtok(list, ","); f; f = strtok(NULL, ",")) {
        uint8_t d[64];
        size_t n = unhex(f, d, sizeof(d));
        size_t m = isotp_on_frame(&t, d, (uint8_t)n);
        if (m) {
            if (o) {
                out[o++] = ' ';
            }
            put_hex(out, &o, sizeof(out), rx, m);
        }
    }
    puts(o ? out : "-");
}

static void handle(char *line)
{
    char *cmd = strtok(line, " \n"), *a1 = strtok(NULL, " \n"), *a2 = strtok(NULL, " \n");
    size_t n = a1 ? unhex(a1, buf, sizeof(buf) - 1) : 0;
    sent_len = 0;
    sent[0] = '\0';
    if (!cmd) {
        puts("?");
    } else if (!strcmp(cmd, "crc")) {
        printf("%04x\n", crc16_ccitt(0, buf, (int)n));
    } else if (!strcmp(cmd, "b64e")) {
        static char s[MAX_IN * 2];
        base64_encode(buf, (int)n, s, 1);
        puts(s);
    } else if (!strcmp(cmd, "b64d")) {
        static uint8_t d[MAX_IN];
        buf[n] = '\0';
        int m = base64_decode((const char *)buf, d);
        if (m < 0) {
            puts("ERR");
        } else {
            answer_hex(d, (size_t)m, "OK ");
        }
    } else if (!strcmp(cmd, "link")) {
        bool took = umcub_link_rx(&t_link, buf, n);
        printf("%d %s\n", took, sent);
    } else if (!strcmp(cmd, "lite")) {
        puts(umcub_lite_rx(&t_lite, buf, n) && sent_len ? sent : "-");
    } else if (!strcmp(cmd, "slot")) {
        answer_hex(&fake_flash[UMCUB_CFG_IMG0_PRIMARY_ADDR - 0x08000000u], strtoul(a1, NULL, 10), "");
    } else if (!strcmp(cmd, "isotp") && a2) {
        isotp_rx((unsigned)strtoul(a1, NULL, 10), a2);
    } else if (!strcmp(cmd, "isotps") && a2) {
        size_t m = unhex(a2, buf, sizeof(buf));
        isotp_t t = { .frame_len = (uint8_t)strtoul(a1, NULL, 10) };
        int rc = isotp_send(&t, buf, m);
        puts(rc ? "ERR" : sent);
    } else if (!strcmp(cmd, "img")) {
        slot_clear();
        memcpy(&fake_flash[UMCUB_CFG_IMG0_PRIMARY_ADDR - 0x08000000u], buf, n);     /* MAX_IN < the slot */
        printf("%d\n", umcub_inspect_verify(0, 0));
    } else {
        puts("?");
    }
    fflush(stdout);
}

int main(void)
{
    static char line[4 * MAX_IN + 64];
    uint8_t uid[12];
    fake_realtime = 1;                          /* isotp.c times frames out after 1 s */
    fake_node_addr = NODE;
    fake_flash_reset();
    umcub_port_uid(uid);
    size_t o = 0;
    char u[32];
    put_hex(u, &o, sizeof(u), uid, sizeof(uid));
    printf("cfg mtu=%u node=%u uid=%s board=%08x\n", (unsigned)UMCUB_CFG_SMP_MTU, NODE, u,
           (unsigned)UMCUB_CFG_BOARD_TYPE);
    fflush(stdout);
    while (fgets(line, sizeof(line), stdin)) {
        handle(line);
    }
    return 0;
}
