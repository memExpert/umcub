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
 *  - responses go to the transport that sent the request;
 *  - a transport in umcub link mode (.link != UMCUB_LINK_PLAIN) carries only
 *    link frames: their DATA payloads (raw SMP or text) join the packet path,
 *    answers are wrapped by transport/link.c - stream transports included.
 * Without any SMP transport (USB DFU only, or none) boot_serial is not built
 * and umcub_recovery_run() drives the same loop itself.
 */
#include <string.h>
#include "umcub_cfg.h"
#include "umcub_log.h"
#include "umcub_port.h"
#include "umcub_transport.h"
#include "umcub_handoff.h"
#include "umcub_cmd.h"
#if UMCUB_CFG_LINK_ANY
#include "umcub_link.h"
#endif
#if UMCUB_CFG_SMP
#include "boot_serial/boot_serial.h"
#endif
#define FRAMES (UMCUB_CFG_LITE_UPLOAD || UMCUB_CFG_PROTO_USER)
#if UMCUB_CFG_SMP || FRAMES
#include "base64/base64.h"
#endif
#if FRAMES
#include "umcub_lite.h"
#endif
/* NLIP line starts and boot_serial_input() (on the include path, used
 * without boot_serial too: plain streams still tell NLIP from text). */
#include "boot_serial_priv.h"

#define NLIP_PKT_START1   SHELL_NLIP_PKT_START1
#define NLIP_PKT_START2   SHELL_NLIP_PKT_START2
#define NLIP_DATA_START1  SHELL_NLIP_DATA_START1
#define NLIP_DATA_START2  SHELL_NLIP_DATA_START2
#if UMCUB_CFG_LINK_ANY && UMCUB_LINK_LINE_MAX > UMCUB_CFG_SMP_MTU
#define LINE_MAX          UMCUB_LINK_LINE_MAX
#else
#define LINE_MAX          UMCUB_CFG_SMP_MTU
#endif
#define LOCK_TIMEOUT_MS   1000u
#define MAX_TRANSPORTS    (UMCUB_TRANSPORT_MAX ? UMCUB_TRANSPORT_MAX : 1)

struct line {
    uint16_t len;
    bool ready;
    char buf[LINE_MAX + 1];
};

/* Line assemblers only for transports that can be streams (UART, USB CDC,
 * board transport); packet transports (CAN, UDP) never use one. */
#define STREAMS           ((UMCUB_CFG_TRANSPORT_UART != 0) + (UMCUB_CFG_TRANSPORT_USB_CDC != 0) + \
                           (UMCUB_CFG_TRANSPORT_USER != 0))
#define MAX_STREAMS       (STREAMS ? STREAMS : 1)
static struct line lines[MAX_STREAMS];
static int locked = -1;          /* stream index receiving a multi-line packet */
static uint32_t locked_at;
static bool responded;
static const umcub_transport_t *active;
static bool is_stream_fn(const umcub_transport_t *t)
{
    return t->read && t->write;
}
#define is_stream is_stream_fn

/* Line of stream transport i (registry index); NULL for packet transports. */
static struct line *line_of(unsigned i)
{
    unsigned n = 0;
    for (unsigned j = 0; j < i; j++) {
        n += is_stream_fn(umcub_transports[j]);
    }
    return is_stream_fn(umcub_transports[i]) && n < MAX_STREAMS ? &lines[n] : NULL;
}

/* Plain stream transport: NLIP lines and typed text go straight through. */
static bool nlip_stream(const umcub_transport_t *t)
{
    return is_stream_fn(t) && t->link == 0;
}

/* One packet to `t`: a link frame in link mode, else the transport's packet. */
static void tx_packet(const umcub_transport_t *t, const uint8_t *data, size_t len)
{
#if UMCUB_CFG_LINK_ANY
    if (t->link) {
        umcub_link_send_data(t, data, len);
        return;
    }
#endif
    if (t->send_packet) {
        (void)t->send_packet(data, len);
    }
}

/* Transport of the packet request being handled (text command or SMP). */
static __attribute__((unused)) const umcub_transport_t *req_from;

bool umcub_mux_request_confidential(void)
{
#if UMCUB_CFG_LINK_ANY
    return req_from && umcub_link_confidential(req_from);
#else
    return false;
#endif
}

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
    if (nlip_stream(cmd_from)) {
        if (text) {
            cmd_from->write((const uint8_t *)text, strlen(text));
        }
        return;
    }
    if (!text) {                    /* flush */
        if (cmd_out_len) {
            tx_packet(cmd_from, (const uint8_t *)cmd_out, cmd_out_len);
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
    req_from = t;
    umcub_cmd_execute(text, len, cmd_reply, in_recovery);
    req_from = NULL;
    cmd_reply(NULL);
}
#endif

/* A raw packet is SMP if its 8-byte header announces exactly the rest. */
static bool is_smp_packet(const uint8_t *p, size_t len)
{
    return len >= 8 && (p[0] & 0x07u) <= 3u && (size_t)((p[2] << 8) | p[3]) == len - 8u;
}

/* SMP only if this build speaks it: without SMP every packet is a lite /
 * board frame or a text command. */
static bool smp_packet(const uint8_t *p, size_t len)
{
    return UMCUB_CFG_SMP && is_smp_packet(p, len);
}

static void touch(const umcub_transport_t *t);

#if FRAMES
/* A recovery protocol (lite upload, board protocol) took a frame: stay in
 * the bootloader (entry window). */
static bool frame_seen;

void umcub_mux_send_frame(const umcub_transport_t *t, uint8_t kind, const uint8_t *frame, size_t len)
{
    if (!nlip_stream(t)) {
        tx_packet(t, frame, len);
        return;
    }
    static char line[2 + 4 * ((512 + 2) / 3) + 2];
    if (len > 512) {
        return;
    }
    line[0] = UMCUB_FRAME_LINE_START;
    line[1] = (char)kind;
    int n = base64_encode(frame, (int)len, &line[2], 1);
    line[2 + n] = '\n';
    t->write((const uint8_t *)line, (size_t)n + 3u);
}

#if UMCUB_CFG_PROTO_USER
void umcub_proto_reply(const umcub_transport_t *t, const uint8_t *data, size_t len)
{
    umcub_mux_send_frame(t, UMCUB_FRAME_USER, data, len);
}
#endif

/* A frame of `kind` (lite upload or board protocol). */
static bool frame_rx(const umcub_transport_t *t, uint8_t kind, const uint8_t *f, size_t len)
{
    bool taken = false;
#if UMCUB_CFG_LITE_UPLOAD
    if (kind == UMCUB_FRAME_LITE) {
        taken = umcub_lite_rx(t, f, len);
    }
#endif
#if UMCUB_CFG_PROTO_USER
    if (!taken && kind == UMCUB_FRAME_USER) {
        taken = umcub_proto_user(t, f, len);
    }
#endif
    if (taken) {
        touch(t);
        frame_seen = true;
    }
    return taken;
}
#endif

/* Raw packets exist only with packet transports or the umcub link (its DATA
 * payloads take this path on streams too); otherwise the packet path and its
 * buffers are left out. */
#define PACKETS (UMCUB_CFG_TRANSPORT_CAN || UMCUB_CFG_TRANSPORT_ETH || UMCUB_CFG_TRANSPORT_USER || \
                 UMCUB_CFG_LINK_ANY)

/* pending raw packet from a packet transport */
static uint8_t pkt_buf[PACKETS ? UMCUB_CFG_SMP_MTU + 1 : 1];
static size_t pkt_len;
static const umcub_transport_t *pkt_from;

#if UMCUB_CFG_SMP
/* NLIP -> raw decoder for responses to packet transports */
static char resp_text[PACKETS ? 160 : 2];
static size_t resp_text_len;
static uint8_t resp_raw[PACKETS ? UMCUB_CFG_SMP_MTU : 1];
static size_t resp_raw_len;
#endif



static bool nlip_start(const char *b, uint8_t c1, uint8_t c2)
{
    return (uint8_t)b[0] == c1 && (uint8_t)b[1] == c2;
}

static bool queue_packet(const umcub_transport_t *t, const uint8_t *pkt, size_t len)
{
    if (!PACKETS || pkt_len != 0 || len == 0 || len > UMCUB_CFG_SMP_MTU) {
        return false;
    }
    memcpy(pkt_buf, pkt, len);
    pkt_from = t;
    pkt_len = len;
    return true;
}

bool umcub_smp_packet_rx(const umcub_transport_t *t, const uint8_t *pkt, size_t len)
{
#if UMCUB_CFG_LINK_ANY
    if (t->link) {
        return umcub_link_rx(t, pkt, len);  /* DATA comes back via umcub_mux_link_rx() */
    }
#endif
    return queue_packet(t, pkt, len);
}

#if UMCUB_CFG_LINK_ANY
bool umcub_mux_link_rx(const umcub_transport_t *t, const uint8_t *payload, size_t len)
{
    return queue_packet(t, payload, len);
}

/* Link mode stream transport: only "0x05 0x0B base64 \n" lines count. */
static void pump_link_stream(unsigned i)
{
    const umcub_transport_t *t = umcub_transports[i];
    struct line *l = line_of(i);
    uint8_t c;
    while (l && t->read(&c, 1) == 1) {
        if (l->len == 0 && c != UMCUB_LINK_LINE_START1) {
            continue;                   /* not a frame: ignored in link mode */
        }
        if (l->len == 1 && c != UMCUB_LINK_LINE_START2) {
            l->len = 0;
            continue;
        }
        if (c == '\n') {
            umcub_link_rx_line(t, &l->buf[2], l->len - 2u);
            l->len = 0;
            continue;
        }
        if (c == '\r') {
            continue;
        }
        if (l->len >= LINE_MAX) {
            l->len = 0;                 /* overlong: drop */
            continue;
        }
        l->buf[l->len++] = (char)c;
    }
}
#endif

#if FRAMES
/* "05 <kind> <base64> \n" on a plain stream transport. */
static void frame_line(const umcub_transport_t *t, char *buf, size_t len)
{
    static uint8_t frame[(LINE_MAX / 4) * 3 + 3];
    if (len < 4) {
        return;
    }
    buf[len - 1] = '\0';                   /* drop the newline */
    if (base64_decode_len(&buf[2]) > (int)sizeof(frame)) {
        return;
    }
    int n = base64_decode(&buf[2], frame);
    if (n > 0) {
        (void)frame_rx(t, (uint8_t)buf[1], frame, (size_t)n);
    }
}
#endif

/* Pull bytes of stream transport i until a full line is assembled. */
static void pump_stream(unsigned i)
{
    const umcub_transport_t *t = umcub_transports[i];
    struct line *l = line_of(i);
    uint8_t c;

    while (l && !l->ready && t->read(&c, 1) == 1) {
        uint8_t first = l->len ? (uint8_t)l->buf[0] : c;
        bool nlip = first == NLIP_PKT_START1 || first == NLIP_DATA_START1 ||
                    (FRAMES && first == 0x05);   /* 05 0C / 05 0D: lite upload / board frame line */
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
#if FRAMES
        if ((uint8_t)l->buf[0] == 0x05) {
            frame_line(t, l->buf, l->len);
            l->len = 0;
            continue;
        }
#endif
        if (l->len > 3 && UMCUB_CFG_SMP) {     /* an NLIP line for boot_serial */
            l->buf[l->len] = '\0';
            l->ready = true;
        } else {
            l->len = 0;
        }
    }
}

/* A packet that is not SMP: lite upload, board protocol or a text command. */
static void other_packet(const umcub_transport_t *t, const uint8_t *p, size_t len)
{
#if FRAMES
    if (frame_rx(t, p[0] == UMCUB_LITE_MAGIC ? UMCUB_FRAME_LITE : UMCUB_FRAME_USER, p, len)) {
        return;
    }
#endif
#if UMCUB_CFG_CMD_ENABLE
    cmd_run(t, (const char *)p, len);
#else
    (void)t;
    (void)p;
    (void)len;
#endif
}

static void pump_all(void)
{
    umcub_transports_poll();
    for (unsigned i = 0; i < umcub_transport_count && i < MAX_TRANSPORTS; i++) {
        if (nlip_stream(umcub_transports[i])) {
            pump_stream(i);
        }
#if UMCUB_CFG_LINK_ANY
        else if (is_stream(umcub_transports[i])) {
            pump_link_stream(i);
        }
#endif
    }
#if UMCUB_CFG_LINK_ANY
    umcub_link_poll();
#endif
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
    if (PACKETS && pkt_len && locked < 0 && !smp_packet(pkt_buf, pkt_len)) {
        other_packet(pkt_from, pkt_buf, pkt_len);
        pkt_len = 0;
        return 0;
    }
#if UMCUB_CFG_SMP
    if (PACKETS && pkt_len && locked < 0) {
        touch(pkt_from);
        resp_text_len = 0;
        resp_raw_len = 0;
        req_from = pkt_from;
        boot_serial_input((char *)pkt_buf, (int)pkt_len);
        req_from = NULL;
        pkt_len = 0;
        return 0;
    }
#endif

    if (locked >= 0 && (uint32_t)(umcub_port_millis() - locked_at) > LOCK_TIMEOUT_MS) {
        locked = -1;
    }

    for (unsigned i = 0; i < umcub_transport_count && i < MAX_TRANSPORTS; i++) {
        struct line *l = line_of(i);
        if (!l || !l->ready || (locked >= 0 && locked != (int)i)) {
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

#if UMCUB_CFG_SMP
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
                tx_packet(active, &resp_raw[2], total - 2u);
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
    if (nlip_stream(active)) {
        active->write((const uint8_t *)ptr, (size_t)cnt);
        if (cnt == 1 && ptr[0] == '\n') {
            responded = true;   /* request on the locked stream was answered */
        }
    } else if (PACKETS && (active->send_packet || active->link)) {
        shim_feed(ptr, cnt);
    }
}

static const struct boot_uart_funcs mux_funcs = {
    .read = mux_read,
    .write = mux_write,
};
#endif /* UMCUB_CFG_SMP */

bool umcub_recovery_wait(uint32_t ms)
{
    uint32_t start = umcub_port_millis();
    do {
        umcub_port_wdg_feed();
        pump_all();
        if (PACKETS && pkt_len && !smp_packet(pkt_buf, pkt_len)) {
            other_packet(pkt_from, pkt_buf, pkt_len);
            pkt_len = 0;
        }
#if FRAMES
        if (frame_seen) {
            frame_seen = false;
            return true;        /* an upload started: stay in the bootloader */
        }
#endif
#if UMCUB_CFG_CMD_ENABLE
        unsigned d = umcub_cmd_take_decision();
        if (d == UMCUB_CMD_STAY) {
            return true;
        }
        if (d == UMCUB_CMD_BOOT_APP) {
            return false;   /* skip the rest of the window */
        }
#endif
        if (PACKETS && pkt_len) {
            return true;
        }
#if UMCUB_CFG_LINK_ANY
        if (umcub_link_take_wakeup()) {
            return true;        /* a host addressed this node */
        }
#endif
        for (unsigned i = 0; i < umcub_transport_count && i < MAX_TRANSPORTS; i++) {
            const struct line *l = line_of(i);
            if (l && l->ready) {
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
#if UMCUB_CFG_SMP
    UMCUB_LOG_INF("recovery mode: waiting for SMP requests");
    boot_serial_start(&mux_funcs);
#else
    UMCUB_LOG_INF("recovery mode");
    static char line[LINE_MAX + 1];
    int newline;
    for (;;) {
        (void)mux_read(line, (int)sizeof(line), &newline);   /* polls USB DFU, commands, timeout */
    }
#endif
    umcub_port_reset();
}

#if defined(UMCUB_HOST_TEST) && UMCUB_CFG_SMP
const struct boot_uart_funcs *umcub_mux_funcs_for_test(void)
{
    return &mux_funcs;
}
#endif

uint8_t umcub_recovery_last_transport(void)
{
    return last_transport;
}
