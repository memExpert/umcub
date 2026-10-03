/*
 * Text commands: table from UMCUB_CFG_CMD_TABLE (umcub_config.h).
 */
#include <stdarg.h>
#include <string.h>
#include "umcub_cfg.h"
#include "umcub_boot.h"
#include "umcub_cmd.h"
#include "umcub_log.h"
#include "umcub_port.h"
#include "umcub_transport.h"
#include "umcub_version.h"
#include "umcub_inspect.h"
#include "bootutil/bootutil_public.h"
#include "bootutil/image.h"
#include "flash_map_backend/flash_map_backend.h"

#if UMCUB_CFG_CMD_ENABLE

typedef struct {
    const char *text;
    unsigned action;
} cmd_entry_t;

static const cmd_entry_t table[] = { UMCUB_CFG_CMD_TABLE };
#define TABLE_N (sizeof(table) / sizeof(table[0]))

static unsigned decision;

__attribute__((weak)) bool umcub_cmd_user(unsigned id, const char *text, umcub_cmd_reply_t reply)
{
    (void)id;
    (void)text;
    (void)reply;
    return false;
}

static bool takes_args(unsigned action)
{
    return action == UMCUB_CMD_VERIFY || action == UMCUB_CMD_HASH || action == UMCUB_CMD_READ;
}

umcub_cmd_match_t umcub_cmd_match(const char *text, size_t len)
{
    bool full = false, prefix = false;
    for (unsigned i = 0; i < TABLE_N; i++) {
        size_t n = strlen(table[i].text);
        if (len <= n && memcmp(table[i].text, text, len) == 0) {
            if (len == n && !takes_args(table[i].action)) {
                full = true;
            } else {
                prefix = true;      /* a longer command starts like this */
            }
        } else if (takes_args(table[i].action) && len > n && memcmp(table[i].text, text, n) == 0 &&
                   text[n] == ' ') {
            prefix = true;          /* typing arguments: wait for Enter */
        }
    }
    /* "r" next to "read": not unambiguous yet, wait for more input / Enter */
    return prefix ? UMCUB_CMD_MATCH_PREFIX : full ? UMCUB_CMD_MATCH_FULL : UMCUB_CMD_MATCH_NONE;
}

/* "<image> <slot> [<off> <len>]", decimal or 0x hex. */
static int parse_args(const char *p, uint32_t v[4])
{
    int n = 0;
    while (*p && n < 4) {
        while (*p == ' ') {
            p++;
        }
        if (!*p) {
            break;
        }
        uint32_t x = 0;
        unsigned base = 10;
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            base = 16;
            p += 2;
        }
        const char *start = p;
        for (;; p++) {
            unsigned d;
            if (*p >= '0' && *p <= '9') d = (unsigned)(*p - '0');
            else if (base == 16 && *p >= 'a' && *p <= 'f') d = (unsigned)(*p - 'a' + 10);
            else if (base == 16 && *p >= 'A' && *p <= 'F') d = (unsigned)(*p - 'A' + 10);
            else break;
            x = x * base + d;
        }
        if (p == start || (*p && *p != ' ')) {
            return -1;
        }
        v[n++] = x;
    }
    return n;
}

unsigned umcub_cmd_take_decision(void)
{
    unsigned d = decision;
    decision = 0;
    return d;
}

static void say(umcub_cmd_reply_t reply, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void say(umcub_cmd_reply_t reply, const char *fmt, ...)
{
#if UMCUB_CFG_CMD_REPLY
    char buf[120];
    va_list ap;
    va_start(ap, fmt);
    int n = umcub_vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof(buf) - 3) {
        n = sizeof(buf) - 3;
    }
    buf[n++] = '\r';
    buf[n++] = '\n';
    buf[n] = '\0';
    reply(buf);
#else
    (void)reply;
    (void)fmt;
#endif
}

/* Let the reply leave (USB/ETH need polling) before resetting. */
static __attribute__((noreturn)) void flush_and_reset(umcub_cmd_reply_t reply)
{
    reply(NULL);
    uint32_t start = umcub_port_millis();
    while ((uint32_t)(umcub_port_millis() - start) < 50u) {
        umcub_transports_poll();
    }
    umcub_port_reset();
}

static void info(umcub_cmd_reply_t reply)
{
    static const char *const modes[] = { "?", "overwrite", "swap-scratch", "swap-move", "swap-offset",
                                         "direct-xip", "direct-xip-revert" };
    say(reply, "umcub %d.%d.%d %s %s images %d", UMCUB_VERSION_MAJOR, UMCUB_VERSION_MINOR,
        UMCUB_VERSION_PATCH, UMCUB_BOARD_NAME, modes[UMCUB_CFG_UPGRADE_MODE], UMCUB_CFG_IMAGE_NUMBER);
    say(reply, "board type 0x%08lx rev %u node %u", (unsigned long)UMCUB_CFG_BOARD_TYPE,
        (unsigned)UMCUB_CFG_BOARD_REV, (unsigned)umcub_node_address());
    for (int img = 0; img < UMCUB_CFG_IMAGE_NUMBER; img++) {
        for (int slot = 0; slot < 2; slot++) {
            const struct flash_area *fa;
            struct image_header h;
            if (flash_area_open((uint8_t)flash_area_id_from_multi_image_slot(img, slot), &fa) ||
                flash_area_read(fa, 0, &h, sizeof(h)) || h.ih_magic != IMAGE_MAGIC) {
                say(reply, "image %d slot %d: empty", img, slot);
                continue;
            }
            struct boot_swap_state st = { 0 };
            (void)boot_read_swap_state(fa, &st);
            /* Same meaning as umcub_is_confirmed(): an image without a trailer
             * cannot be reverted, so it counts as confirmed. */
            const char *state = st.magic != BOOT_MAGIC_GOOD ? (slot == 0 ? " confirmed" : "")
                              : st.image_ok == BOOT_FLAG_SET ? " confirmed"
                              : slot == 0 ? " test" : " pending";
            say(reply, "image %d slot %d: %u.%u.%u+%lu%s", img, slot, h.ih_ver.iv_major, h.ih_ver.iv_minor,
                h.ih_ver.iv_revision, (unsigned long)h.ih_ver.iv_build_num, state);
        }
    }
}

static const char hexd[] = "0123456789abcdef";

static void inspect(unsigned action, const char *args, umcub_cmd_reply_t reply)
{
    uint32_t v[4] = { 0, 0, 0, 0 };
    int n = parse_args(args, v);
    if (n < 2 || (action == UMCUB_CMD_READ && n < 4)) {
        say(reply, "? usage: <image> <slot>%s", action == UMCUB_CMD_VERIFY ? "" :
            action == UMCUB_CMD_HASH ? " [<off> <len>]" : " <off> <len>");
        return;
    }
    int img = (int)v[0], slot = (int)v[1];
    if (action == UMCUB_CMD_VERIFY) {
        int rc = umcub_inspect_verify(img, slot);
        say(reply, "%s", rc == 0 ? "ok valid" : rc == UMCUB_ENOTSUP ? "bad no image" :
                          rc == UMCUB_EINVAL ? "? bad image/slot" : "bad hash or signature");
        return;
    }
    if (action == UMCUB_CMD_HASH) {
        uint8_t h[32];
        uint32_t hashed = 0;
        int rc = umcub_inspect_hash(img, slot, n >= 3 ? v[2] : 0, n >= 4 ? v[3] : 0, h, &hashed);
        if (rc == UMCUB_EPERM) {
            say(reply, "? only whole-image hashes outside an encrypted session (UMCUB_CFG_ENCRYPT_IMAGES)");
            return;
        }
        if (rc) {
            say(reply, "? hash failed %d", rc);
            return;
        }
        char hex[65];
        for (int i = 0; i < 32; i++) {
            hex[2 * i] = hexd[h[i] >> 4];
            hex[2 * i + 1] = hexd[h[i] & 15];
        }
        hex[64] = '\0';
        say(reply, "sha256 %s len %lu", hex, (unsigned long)hashed);
        return;
    }
#if UMCUB_CFG_READBACK
    /* hex dump, 32 bytes per line: "<offset hex> <data hex>" */
    uint32_t off = v[2], len = v[3];
    while (len) {
        uint8_t b[32];
        uint32_t k = len > sizeof(b) ? sizeof(b) : len;
        int rc = umcub_inspect_read(img, slot, off, b, k);
        if (rc == UMCUB_EPERM) {
            say(reply, "? readback only in an encrypted session (UMCUB_CFG_ENCRYPT_IMAGES)");
            return;
        }
        if (rc) {
            say(reply, "? read failed at %lu", (unsigned long)off);
            return;
        }
        char line[2 * 32 + 1];
        for (uint32_t i = 0; i < k; i++) {
            line[2 * i] = hexd[b[i] >> 4];
            line[2 * i + 1] = hexd[b[i] & 15];
        }
        line[2 * k] = '\0';
        say(reply, "%08lx %s", (unsigned long)off, line);
        off += k;
        len -= k;
        umcub_port_wdg_feed();
    }
#else
    say(reply, "? readback disabled (UMCUB_CFG_READBACK)");
#endif
}

void umcub_cmd_execute(const char *text, size_t len, umcub_cmd_reply_t reply, bool in_recovery)
{
    char cmd[48];
    while (len && (text[len - 1] == '\r' || text[len - 1] == '\n' || text[len - 1] == ' ')) {
        len--;
    }
    if (!len) {
        return;
    }
    if (len >= sizeof(cmd)) {
        len = sizeof(cmd) - 1;
    }
    memcpy(cmd, text, len);
    cmd[len] = '\0';

    for (unsigned i = 0; i < TABLE_N; i++) {
        size_t n = strlen(table[i].text);
        bool exact = strcmp(table[i].text, cmd) == 0;
        bool with_args = takes_args(table[i].action) && strncmp(table[i].text, cmd, n) == 0 && cmd[n] == ' ';
        if (!exact && !with_args) {
            continue;
        }
        unsigned a = table[i].action;
        if (takes_args(a)) {
            UMCUB_LOG_INF("command \"%s\"", cmd);
            inspect(a, cmd + n, reply);
            return;
        }
        UMCUB_LOG_INF("command \"%s\"", cmd);
        switch (a) {
        case UMCUB_CMD_BOOT_APP:
            say(reply, "ok boot");
            if (in_recovery) {
                umcub_handoff_request(UMCUB_REQ_BOOT_APP, 0);
                flush_and_reset(reply);
            }
            decision = UMCUB_CMD_BOOT_APP;
            return;
        case UMCUB_CMD_STAY:
            say(reply, "ok stay");
            decision = UMCUB_CMD_STAY;
            return;
        case UMCUB_CMD_RESET:
            say(reply, "ok reset");
            flush_and_reset(reply);
        case UMCUB_CMD_INFO:
            info(reply);
            return;
        default:
            if (a >= UMCUB_CMD_USER(0) && umcub_cmd_user(a - UMCUB_CMD_USER(0), cmd, reply)) {
                return;
            }
            break;
        }
        break;
    }
    say(reply, "? %s", cmd);
}

#endif /* UMCUB_CFG_CMD_ENABLE */
