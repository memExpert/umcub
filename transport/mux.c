/*
 * SMP request multiplexer between the transports and MCUboot boot_serial.
 *
 * boot_serial reads NLIP-framed lines through boot_uart_funcs.read() and
 * answers through .write(). Here:
 *  - every stream transport gets its own line assembler; complete NLIP lines
 *    are handed over one at a time, and while a multi-line packet is being
 *    received from one stream the others wait (lock);
 *  - raw SMP packets from packet transports are fed straight into
 *    boot_serial_input(); the NLIP/base64 response boot_serial produces is
 *    decoded back into a raw packet for the transport (smp shim);
 *  - responses go to the transport that sent the request.
 */
#include <string.h>
#include "umcub_cfg.h"
#include "umcub_log.h"
#include "umcub_port.h"
#include "umcub_transport.h"
#include "umcub_handoff.h"
#include "boot_serial/boot_serial.h"
#include "base64/base64.h"
#include "umcub_cmd.h"

/* boot_serial_priv.h is not on the public include path. */
void boot_serial_input(char *buf, int len);

#define NLIP_PKT_START1   6
#define NLIP_PKT_START2   9
#define NLIP_DATA_START1  4
#define NLIP_DATA_START2  20
#define LINE_MAX          UMCUB_CFG_SMP_MTU
#define LOCK_TIMEOUT_MS   1000u
#define MAX_TRANSPORTS    4

struct line {
    uint16_t len;
    bool ready;
    char buf[LINE_MAX + 1];
};

static struct line lines[MAX_TRANSPORTS];
static int locked = -1;          /* stream index receiving a multi-line packet */
static uint32_t locked_at;
static bool responded;
static const umcub_transport_t *active;
static bool is_stream_fn(const umcub_transport_t *t)
{
    return t->read && t->write;
}
#define is_stream is_stream_fn

static uint8_t last_transport;
static uint32_t last_activity;
static bool in_recovery;

#define CMD_MAX 47

#if UMCUB_CFG_CMD_ENABLE
static const umcub_transport_t *cmd_from;

/* Packet transports get the whole reply in one packet. */
static char cmd_out[512];
static size_t cmd_out_len;

static void cmd_reply(const char *text)
{
    if (is_stream_fn(cmd_from)) {
        if (text) {
            cmd_from->write((const uint8_t *)text, strlen(text));
        }
        return;
    }
    if (!text) {                    /* flush */
        if (cmd_out_len && cmd_from->send_packet) {
            cmd_from->send_packet((const uint8_t *)cmd_out, cmd_out_len);
        }
        cmd_out_len = 0;
        return;
    }
    size_t n = strlen(text);
    if (n > sizeof(cmd_out) - cmd_out_len) {
        n = sizeof(cmd_out) - cmd_out_len;
    }
    memcpy(&cmd_out[cmd_out_len], text, n);
    cmd_out_len += n;
}

static void cmd_run(const umcub_transport_t *t, const char *text, size_t len)
{
    cmd_from = t;
    cmd_out_len = 0;
    last_activity = umcub_port_millis();
    umcub_cmd_execute(text, len, cmd_reply, in_recovery);
    cmd_reply(NULL);
}
#endif

/* A raw packet is SMP if its 8-byte header announces exactly the rest. */
static bool is_smp_packet(const uint8_t *p, size_t len)
{
    return len >= 8 && (p[0] & 0x07u) <= 3u && (size_t)((p[2] << 8) | p[3]) == len - 8u;
}

/* pending raw packet from a packet transport */
static uint8_t pkt_buf[UMCUB_CFG_SMP_MTU + 1];
static size_t pkt_len;
static const umcub_transport_t *pkt_from;

/* NLIP -> raw decoder for responses to packet transports */
static char resp_text[160];
static size_t resp_text_len;
static uint8_t resp_raw[UMCUB_CFG_SMP_MTU];
static size_t resp_raw_len;



static bool nlip_start(const char *b, uint8_t c1, uint8_t c2)
{
    return (uint8_t)b[0] == c1 && (uint8_t)b[1] == c2;
}

bool umcub_smp_packet_rx(const umcub_transport_t *t, const uint8_t *pkt, size_t len)
{
    if (pkt_len != 0 || len == 0 || len > UMCUB_CFG_SMP_MTU) {
        return false;
    }
    memcpy(pkt_buf, pkt, len);
    pkt_from = t;
    pkt_len = len;
    return true;
}

/* Pull bytes of stream transport i until a full line is assembled. */
static void pump_stream(unsigned i)
{
    const umcub_transport_t *t = umcub_transports[i];
    struct line *l = &lines[i];
    uint8_t c;

    while (!l->ready && t->read(&c, 1) == 1) {
        bool nlip = l->len ? ((uint8_t)l->buf[0] == NLIP_PKT_START1 || (uint8_t)l->buf[0] == NLIP_DATA_START1)
                           : (c == NLIP_PKT_START1 || c == NLIP_DATA_START1);
        if (!nlip) {
            /* Console text: a configured command or noise. */
#if UMCUB_CFG_CMD_ENABLE
            if (c == '\r' || c == '\n') {
                if (l->len) {
                    cmd_run(t, l->buf, l->len);
                }
                l->len = 0;
                continue;
            }
            if (l->len >= CMD_MAX) {
                l->len = 0;
            }
            l->buf[l->len++] = (char)c;
#if UMCUB_CFG_CMD_IMMEDIATE
            umcub_cmd_match_t m = umcub_cmd_match(l->buf, l->len);
            if (m == UMCUB_CMD_MATCH_FULL) {
                size_t n = l->len;
                l->len = 0;
                cmd_run(t, l->buf, n);
            }
            /* Commands are recognised from the start of a line only (a letter
             * inside other text must not fire one); unknown text accumulates
             * until Enter and is answered with "? text". */
#endif
#else
            l->len = 0;
#endif
            continue;
        }
        if (c == '\r') {
            continue;
        }
        if (l->len >= LINE_MAX) {
            l->len = 0;             /* overlong garbage: drop */
        }
        l->buf[l->len++] = (char)c;
        if (c != '\n') {
            continue;
        }
        if (l->len > 3) {
            l->buf[l->len] = '\0';
            l->ready = true;
        } else {
            l->len = 0;
        }
    }
}

static void pump_all(void)
{
    umcub_transports_poll();
    for (unsigned i = 0; i < umcub_transport_count && i < MAX_TRANSPORTS; i++) {
        if (is_stream(umcub_transports[i])) {
            pump_stream(i);
        }
    }
}

void umcub_handoff_note_transport(uint8_t id);

static void touch(const umcub_transport_t *t)
{
    active = t;
    if (last_transport != t->id) {
        umcub_handoff_note_transport(t->id);
    }
    last_transport = t->id;
    last_activity = umcub_port_millis();
}

static int mux_read(char *str, int cnt, int *newline)
{
    *newline = 0;
    umcub_port_wdg_feed();
    pump_all();

#if UMCUB_CFG_RECOVERY_TIMEOUT_MS > 0
    if ((uint32_t)(umcub_port_millis() - last_activity) > UMCUB_CFG_RECOVERY_TIMEOUT_MS) {
        UMCUB_LOG_INF("recovery: idle timeout, reset");
        umcub_port_delay_ms(10);
        umcub_port_reset();
    }
#endif

    if (responded) {
        responded = false;
        locked = -1;
    }

    /* A raw packet waits while a stream is in the middle of a multi-line
     * packet: its response must not release that stream's lock. */
    if (pkt_len && locked < 0 && !is_smp_packet(pkt_buf, pkt_len)) {
#if UMCUB_CFG_CMD_ENABLE
        cmd_run(pkt_from, (const char *)pkt_buf, pkt_len);
#endif
        pkt_len = 0;
        return 0;
    }
    if (pkt_len && locked < 0) {
        touch(pkt_from);
        resp_text_len = 0;
        resp_raw_len = 0;
        boot_serial_input((char *)pkt_buf, (int)pkt_len);
        pkt_len = 0;
        return 0;
    }

    if (locked >= 0 && (uint32_t)(umcub_port_millis() - locked_at) > LOCK_TIMEOUT_MS) {
        locked = -1;
    }

    for (unsigned i = 0; i < umcub_transport_count && i < MAX_TRANSPORTS; i++) {
        struct line *l = &lines[i];
        if (!l->ready || (locked >= 0 && locked != (int)i)) {
            continue;
        }
        int len = l->len;
        if (len >= cnt) {
            l->ready = false;       /* cannot happen with matching sizes */
            l->len = 0;
            return 0;
        }
        memcpy(str, l->buf, (size_t)len + 1u);
        l->ready = false;
        l->len = 0;
        if (nlip_start(str, NLIP_PKT_START1, NLIP_PKT_START2)) {
            locked = (int)i;
        }
        locked_at = umcub_port_millis();
        touch(umcub_transports[i]);
        *newline = 1;
        return len;
    }
    return 0;
}

/* Decode boot_serial's NLIP output and send it as one raw SMP packet. */
static void shim_feed(const char *p, int cnt)
{
    for (int k = 0; k < cnt; k++) {
        char c = p[k];
        if (c != '\n') {
            if (resp_text_len < sizeof(resp_text) - 1u) {
                resp_text[resp_text_len++] = c;
            }
            continue;
        }
        resp_text[resp_text_len] = '\0';
        if (resp_text_len > 2) {
            int room = (int)(sizeof(resp_raw) - resp_raw_len);
            if (base64_decode_len(&resp_text[2]) <= room) {
                int n = base64_decode(&resp_text[2], &resp_raw[resp_raw_len]);
                if (n > 0) {
                    resp_raw_len += (size_t)n;
                }
            }
        }
        resp_text_len = 0;

        if (resp_raw_len >= 2) {
            size_t total = ((size_t)resp_raw[0] << 8) | resp_raw[1];
            if (resp_raw_len >= total + 2u && total >= 2u) {
                /* [len:2][hdr+payload][crc:2] -> [hdr+payload] */
                active->send_packet(&resp_raw[2], total - 2u);
                resp_raw_len = 0;
            }
        }
    }
}

static void mux_write(const char *ptr, int cnt)
{
    if (!active || cnt <= 0) {
        return;
    }
    if (is_stream(active)) {
        active->write((const uint8_t *)ptr, (size_t)cnt);
        if (cnt == 1 && ptr[0] == '\n') {
            responded = true;   /* request on the locked stream was answered */
        }
    } else if (active->send_packet) {
        shim_feed(ptr, cnt);
    }
}

static const struct boot_uart_funcs mux_funcs = {
    .read = mux_read,
    .write = mux_write,
};

bool umcub_recovery_wait(uint32_t ms)
{
    uint32_t start = umcub_port_millis();
    do {
        umcub_port_wdg_feed();
        pump_all();
        if (pkt_len && !is_smp_packet(pkt_buf, pkt_len)) {
#if UMCUB_CFG_CMD_ENABLE
            cmd_run(pkt_from, (const char *)pkt_buf, pkt_len);
#endif
            pkt_len = 0;
        }
#if UMCUB_CFG_CMD_ENABLE
        unsigned d = umcub_cmd_take_decision();
        if (d == UMCUB_CMD_STAY) {
            return true;
        }
        if (d == UMCUB_CMD_BOOT_APP) {
            return false;   /* skip the rest of the window */
        }
#endif
        if (pkt_len) {
            return true;
        }
        for (unsigned i = 0; i < umcub_transport_count && i < MAX_TRANSPORTS; i++) {
            if (lines[i].ready) {
                return true;
            }
        }
    } while ((uint32_t)(umcub_port_millis() - start) < ms);
    return false;
}

__attribute__((noreturn)) void umcub_recovery_run(void)
{
    last_activity = umcub_port_millis();
    in_recovery = true;
    UMCUB_LOG_INF("recovery mode: waiting for SMP requests");
    boot_serial_start(&mux_funcs);
    umcub_port_reset();
}

#ifdef UMCUB_HOST_TEST
const struct boot_uart_funcs *umcub_mux_funcs_for_test(void)
{
    return &mux_funcs;
}
#endif

uint8_t umcub_recovery_last_transport(void)
{
    return last_transport;
}
