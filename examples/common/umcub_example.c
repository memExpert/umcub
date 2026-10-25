/*
 * The board-independent part of the umcub console examples (see
 * umcub_example.h). Uses the umcub application API and the ex_* functions of
 * the board only - no device header.
 */
#include "umcub_example.h"
#include "umcub.h"

#ifndef APP_VERSION_STR
#define APP_VERSION_STR "?"
#endif

static const char *const reasons[] = { "normal", "upgraded", "reverted", "after recovery" };
static const char *const transports[] = { "-", "uart", "usb-cdc", "usb-dfu", "can", "eth", "app", "user" };

/* Timeouts of the flash driver in umcub_slot_* (instead of the library's DWT default). */
uint32_t umcub_port_millis(void)
{
    return ex_millis();
}

void ex_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            ex_putc('\r');
        }
        ex_putc(*s++);
    }
}

void ex_put_u32(uint32_t v)
{
    char b[11];
    int i = 10;
    b[i] = 0;
    do {
        b[--i] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    ex_puts(&b[i]);
}

void ex_put_hex(uint32_t v)
{
    ex_puts("0x");
    for (int s = 28; s >= 0; s -= 4) {
        ex_putc("0123456789abcdef"[(v >> s) & 0xFu]);
    }
}

static void put_ver(const umcub_version_t *v)
{
    ex_put_u32(v->major);
    ex_putc('.');
    ex_put_u32(v->minor);
    ex_putc('.');
    ex_put_u32(v->revision);
    ex_putc('+');
    ex_put_u32(v->build);
}

static void show_info(const char *name)
{
    umcub_version_t v;
    ex_puts("\napp " APP_VERSION_STR " (");
    ex_puts(name);
    ex_puts(")\n");
    if (umcub_boot_version(&v) == 0) {
        ex_puts("bootloader: ");
        put_ver(&v);
        ex_puts("\n");
    } else {
        ex_puts("bootloader: not found\n");
    }
    const umcub_handoff_t *h = umcub_boot_info();
    if (!h) {
        ex_puts("no handoff data (started without bootloader?)\n");
        return;
    }
    ex_puts("boot reason: ");
    ex_puts(h->boot_reason < 4 ? reasons[h->boot_reason] : "?");
    ex_puts(", last update via ");
    ex_puts(h->last_transport < 8 ? transports[h->last_transport] : "?");
    ex_puts(", reset cause ");
    ex_put_u32(h->reset_cause);
    ex_puts("\nboard type ");
    ex_put_hex(h->board_type);
    ex_puts(" rev ");
    ex_put_u32(h->board_rev);
    ex_puts(", node address ");
    ex_put_u32(h->node_addr);
    ex_puts("\n");
    for (unsigned i = 0; i < h->image_count && i < UMCUB_MAX_IMAGES; i++) {
        ex_puts("image ");
        ex_put_u32(i);
        ex_puts(": ");
        if (h->image_addr[i] == 0) {
            ex_puts("not running (no valid image)\n");
            continue;
        }
        put_ver(&h->image_version[i]);
        ex_puts(" @ ");
        ex_put_hex(h->image_addr[i]);
        ex_puts(umcub_is_confirmed((int)i) == 1 ? " confirmed\n" : " NOT confirmed\n");
    }
}

static bool getc_timeout(uint8_t *c, uint32_t timeout)
{
    uint32_t start = ex_millis();
    while ((uint32_t)(ex_millis() - start) < timeout) {
        if (ex_getc(c)) {
            return true;
        }
    }
    return false;
}

/* 'u' <image:u8, bit 7 = primary slot> <size:u32 le> then chunks of up to
 * 1024 bytes; every chunk acknowledged with 'K' (or 'E'), final 'D' (done) or 'E'. */
static void upload(void)
{
    static uint8_t chunk[1024];
    uint8_t hdr[5];
    for (int i = 0; i < 5; i++) {
        if (!getc_timeout(&hdr[i], 2000)) {
            ex_putc('E');
            return;
        }
    }
    int image = hdr[0] & 0x7F;
    int slot = (hdr[0] & 0x80) ? UMCUB_SLOT_PRIMARY : UMCUB_SLOT_DEFAULT;
    uint32_t size = hdr[1] | (uint32_t)hdr[2] << 8 | (uint32_t)hdr[3] << 16 | (uint32_t)hdr[4] << 24;
    umcub_slot_writer_t w;
    int rc = umcub_slot_begin(&w, image, slot, size);
    if (rc != 0) {
        ex_putc('E');
        ex_puts(" slot_begin rc=-");
        ex_put_u32((uint32_t)-rc);
        ex_puts("\n");
        return;
    }
    ex_putc('K');
    for (uint32_t done = 0; done < size;) {
        uint32_t n = size - done > sizeof(chunk) ? sizeof(chunk) : size - done;
        for (uint32_t i = 0; i < n; i++) {
            if (!getc_timeout(&chunk[i], 2000)) {
                umcub_slot_abort(&w);
                ex_putc('E');
                return;
            }
        }
        if (umcub_slot_write(&w, chunk, n) != 0) {
            umcub_slot_abort(&w);
            ex_putc('E');
            return;
        }
        done += n;
        ex_putc('K');
    }
    ex_putc(umcub_slot_finish(&w, true, false) == 0 ? 'D' : 'E');
}

static void delay_ms(uint32_t ms)
{
    uint32_t start = ex_millis();
    while ((uint32_t)(ex_millis() - start) < ms) {
    }
}

__attribute__((noreturn)) static void reset(void)
{
    __asm volatile("dsb");
    *(volatile uint32_t *)0xE000ED0Cu = 0x05FA0004u;   /* SCB->AIRCR: VECTKEY | SYSRESETREQ */
    __asm volatile("dsb");
    for (;;) {
    }
}

void ex_run(const char *name)
{
#ifdef APP_NODE_ADDR
    /* Address on a shared bus, used by the bootloader from the next reset on;
     * it beats UMCUB_CFG_NODE_ADDR_FLASH. Built with APP_NODE_ADDR=none: not
     * set, the bootloader keeps its own. */
    umcub_set_node_address(APP_NODE_ADDR);
#endif
    show_info(name);
    ex_puts("keys: i c b u w r\n");
    uint32_t last = ex_millis();
    bool feed = true;
    for (;;) {
        uint8_t c;
        if (ex_getc(&c)) {
            switch (c) {
            case 'i': show_info(name); break;
            case 'c': ex_puts(umcub_confirm() == 0 ? "confirmed\n" : "confirm failed\n"); break;
            case 'b': ex_puts("-> bootloader\n"); delay_ms(5); umcub_enter_bootloader(0x1234); break;
            case 'u': upload(); break;
            case 'w': ex_puts("watchdog: not fed any more\n"); feed = false; break;
            case 'r': delay_ms(5); reset();
            default: break;
            }
        }
        if (feed) {
            ex_watchdog_feed();     /* once the bootloader started it, it cannot be stopped */
        }
        if ((uint32_t)(ex_millis() - last) >= 250u) {
            last = ex_millis();
            ex_led_toggle();
        }
    }
}
