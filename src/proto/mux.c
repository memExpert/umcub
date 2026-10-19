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
 *    answers are wrapped by src/proto/link.c - stream transports included.
 * Without any SMP transport (USB DFU only, or none) boot_serial is not built
 * and umcub_recovery_run() drives the same loop itself.
 */
#include <string.h>
#include "umcub_cfg.h"
#include "umcub_log.h"
#include "umcub_port.h"
#include "umcub_proto.h"
#include "umcub_bytes.h"
#include "umcub_handoff.h"
#include "umcub_state.h"
#include "umcub_cmd.h"
#include "umcub_inspect.h"
#include "umcub_led.h"
#if UMCUB_CFG_LINK_ANY
#include "umcub_link.h"
#endif
#if UMCUB_CFG_SMP
#include "boot_serial/boot_serial.h"
#endif
#define FRAMES (UMCUB_CFG_LITE_UPLOAD || UMCUB_CFG_PROTO_USER)
#if UMCUB_CFG_SMP || FRAMES
#include "base64/base64.h"
#include "crc/crc16.h"
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
static const umcub_transport_t *active;
static bool is_stream(const umcub_transport_t *t)
{
    return t->read && t->write;
}

/* Line of stream transport i (registry index); NULL for packet transports. */
static struct line *line_of(unsigned i)
{
    unsigned n = 0;
    for (unsigned j = 0; j < i; j++) {
        n += is_stream(umcub_transports[j]);
    }
    return is_stream(umcub_transports[i]) && n < MAX_STREAMS ? &lines[n] : NULL;
}

/* Raw packets exist only with packet transports or the umcub link (its DATA
 * payloads take this path on streams too); otherwise the packet path and its
 * buffers are left out. */
#define PACKETS (UMCUB_CFG_TRANSPORT_CAN || UMCUB_CFG_TRANSPORT_ETH || UMCUB_CFG_TRANSPORT_USER || \
                 UMCUB_CFG_LINK_ANY)

/* Plain stream transport: NLIP lines and typed text go straight through. */
static bool nlip_stream(const umcub_transport_t *t)
{
    return is_stream(t) && t->link == 0;
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

bool umcub_inspect_confidential(void)
{
#if UMCUB_CFG_LINK_ANY
    return req_from && umcub_link_confidential(req_from);
#else
    return false;
#endif
}

void umcub_mux_flush(uint32_t ms)
{
#if UMCUB_CFG_LINK_ANY
    /* The request is answered: what runs from the polling below (a USB DFU
     * readback) must not inherit its encrypted session. */
    req_from = NULL;
#endif
    uint32_t start = umcub_port_millis();
    while ((uint32_t)(umcub_port_millis() - start) < ms) {
        umcub_transports_poll();
    }
}

static uint32_t last_activity;
static bool in_recovery;

#define CMD_MAX 47

#if UMCUB_CFG_CMD_ENABLE
static const umcub_transport_t *cmd_from;

/* Packet transports get the whole reply in one packet. */
static char cmd_out[PACKETS ? 512 : 1];
static size_t cmd_out_len;

static void cmd_reply(const char *text)
{
    if (!PACKETS || nlip_stream(cmd_from)) {
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
    umcub_led_activity();
    req_from = t;
    umcub_cmd_execute(text, len, cmd_reply, in_recovery);
    req_from = NULL;
    cmd_reply(NULL);
}
#endif

/* A raw packet is SMP if its 8-byte header announces exactly the rest and
 * this build speaks SMP: without it every packet is a lite / board frame or
 * a text command. */
static bool smp_packet(const uint8_t *p, size_t len)
{
    return UMCUB_CFG_SMP && len >= 8 && (p[0] & 0x07u) <= 3u && umcub_get_be16(p + 2) == len - 8u;
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

#if UMCUB_CFG_SMP && UMCUB_CFG_INSPECT && UMCUB_CFG_SMP_PARAMS
/* MCUMGR_PARAMETERS (os group 0, id 6, read): boot_serial answers ENOTSUP to
 * os commands it does not know and gives only other groups to the per-user
 * hook, so the request is renamed to the per-user group on its way in;
 * src/proto/smp_inspect.c answers it under its real name. Hosts (smpmgr) then
 * size serial packets to the buffer. h: SMP header (op, flags, len[2],
 * group[2], seq, id). */
static bool rename_params(uint8_t *h, size_t len)
{
    if (len < 8 || (h[0] & 7u) != 0 || h[4] != 0 || h[5] != 0 || h[7] != 6) {
        return false;
    }
    umcub_put_be16(&h[4], UMCUB_CFG_SMP_INSPECT_GROUP);
    h[7] = UMCUB_SMP_PARAMS_ID;
    return true;
}

/* The same in a one-line NLIP packet "06 09 base64([len:2][hdr+payload][crc:2]) \n",
 * re-encoded in place (same length). */
static void line_rename_params(char *line, int len)
{
    uint8_t f[18];
    if (len > 2 + 24 + 1 || !nlip_start(line, NLIP_PKT_START1, NLIP_PKT_START2)) {
        return;
    }
    int n = base64_decode(&line[2], f);
    if (n < 12 || umcub_get_be16(f) != n - 2 || !rename_params(&f[2], (size_t)n - 4u)) {
        return;
    }
    uint16_t crc = crc16_ccitt(CRC16_INITIAL_CRC, &f[2], n - 4);
    umcub_put_be16(&f[n - 2], crc);
    (void)base64_encode(f, n, &line[2], 1);
    line[len - 1] = '\n';
}
#else
#define rename_params(h, len) ((void)0)
#define line_rename_params(line, len) ((void)0)
#endif

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

/* Pull bytes of stream transport i until a full line is assembled. A plain
 * stream carries NLIP lines for boot_serial, frame lines and typed text; a
 * stream in umcub link mode only "0x05 0x0B base64 \n" link frame lines. */
static void pump_stream(unsigned i)
{
    const umcub_transport_t *t = umcub_transports[i];
    struct line *l = line_of(i);
#if UMCUB_CFG_LINK_ANY
    bool link = !nlip_stream(t);
#endif
    uint8_t c;

    while (l && !l->ready && t->read(&c, 1) == 1) {
#if UMCUB_CFG_LINK_ANY
        if (link) {
            if ((l->len == 0 && c != UMCUB_LINK_LINE_START1) || (l->len == 1 && c != UMCUB_LINK_LINE_START2)) {
                l->len = 0;             /* not a frame: ignored in link mode */
                continue;
            }
        } else
#endif
        {
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
                /* Commands are recognised from the start of a line only (a
                 * letter inside other text must not fire one); unknown text
                 * accumulates until Enter and is answered with "? text". */
#endif
#else
                l->len = 0;
#endif
                continue;
            }
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
#if UMCUB_CFG_LINK_ANY
        if (link) {
            if (l->len >= 3) {                  /* not after an overlong line's reset */
                umcub_link_rx_line(t, &l->buf[2], l->len - 3u);     /* base64 without the newline */
            }
            l->len = 0;
            continue;
        }
#endif
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
    umcub_led_poll();
    umcub_transports_poll();
    for (unsigned i = 0; i < umcub_transport_count && i < MAX_TRANSPORTS; i++) {
        if (is_stream(umcub_transports[i])) {
            pump_stream(i);
        }
    }
#if UMCUB_CFG_LINK_ANY
    umcub_link_poll();
#endif
}

static void touch(const umcub_transport_t *t)
{
    if (!active || active->id != t->id) {
        umcub_handoff_note_transport(t->id);
    }
    active = t;
    last_activity = umcub_port_millis();
    umcub_led_activity();
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

    /* A raw packet waits while a stream is in the middle of a multi-line
     * packet: its response must not release that stream's lock. */
    if (PACKETS && pkt_len && locked < 0) {
        if (!smp_packet(pkt_buf, pkt_len)) {
            other_packet(pkt_from, pkt_buf, pkt_len);
        }
#if UMCUB_CFG_SMP
        else {
            touch(pkt_from);
            resp_text_len = 0;
            resp_raw_len = 0;
            req_from = pkt_from;
            (void)rename_params(pkt_buf, pkt_len);
            boot_serial_input((char *)pkt_buf, (int)pkt_len);
            req_from = NULL;
        }
#endif
        pkt_len = 0;
        return 0;
    }

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
        line_rename_params(str, len);
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
            size_t total = umcub_get_be16(resp_raw);
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
            locked = -1;        /* request on the locked stream was answered */
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
    return active ? active->id : 0;
}
