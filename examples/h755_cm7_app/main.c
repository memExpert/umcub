/*
 * umcub example application, CM7 of NUCLEO-H755ZI-Q.
 *
 * Blinks LD1 and talks on the ST-LINK VCP (115200):
 *   i  show bootloader / image information
 *   c  confirm this image (swap modes: otherwise reverted on next reset)
 *   b  reboot into the bootloader's recovery mode
 *   u  receive an image and write it into a slot (tools/app_upload.py)
 *   r  reset
 */
#include <string.h>
#include "stm32h7xx.h"
#include "h7_example.h"
#include "umcub.h"

static const char *const reasons[] = { "normal", "upgraded", "reverted", "after recovery" };
static const char *const transports[] = { "-", "uart", "usb-cdc", "usb-dfu", "can", "eth", "app", "user" };

static void put_ver(const umcub_version_t *v)
{
    ex_put_u32(v->major);
    ex_uart_putc('.');
    ex_put_u32(v->minor);
    ex_uart_putc('.');
    ex_put_u32(v->revision);
    ex_uart_putc('+');
    ex_put_u32(v->build);
}

static void show_info(void)
{
    umcub_version_t v;
    ex_puts("\napp " APP_VERSION_STR " (CM7)\n");
    if (umcub_boot_version(&v) == 0) {
        ex_puts("bootloader: ");
        put_ver(&v);
        ex_puts("\n");
    } else {
        ex_puts("bootloader: not found\n");
    }
    const umcub_handoff_t *h = umcub_boot_info();
    if (h) {
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
    } else {
        ex_puts("no handoff data (started without bootloader?)\n");
    }
}

static bool getc_timeout(uint8_t *c, uint32_t ms)
{
    for (uint32_t t = 0; t < ms * 10u; t++) {
        if (ex_uart_getc(c)) {
            return true;
        }
        for (volatile int d = 0; d < 6; d++) {
        }
    }
    return false;
}

/* 'u' <image:u8> <size:u32 le> then chunks of up to 1024 bytes; every chunk
 * acknowledged with 'K' (or 'E'), final 'D' (done) or 'E'. */
static void upload(void)
{
    static uint8_t chunk[1024];
    uint8_t hdr[5];
    for (int i = 0; i < 5; i++) {
        if (!getc_timeout(&hdr[i], 2000)) {
            ex_uart_putc('E');
            return;
        }
    }
    int image = hdr[0];
    uint32_t size = hdr[1] | (uint32_t)hdr[2] << 8 | (uint32_t)hdr[3] << 16 | (uint32_t)hdr[4] << 24;

    umcub_slot_writer_t w;
    int rc = umcub_slot_begin(&w, image, UMCUB_SLOT_DEFAULT, size);
    if (rc != 0) {
        ex_uart_putc('E');
        ex_puts(" slot_begin rc=-");
        ex_put_u32((uint32_t)-rc);
        ex_puts("\n");
        return;
    }
    ex_uart_putc('K');
    uint32_t done = 0;
    while (done < size) {
        uint32_t n = size - done > sizeof(chunk) ? sizeof(chunk) : size - done;
        for (uint32_t i = 0; i < n; i++) {
            if (!getc_timeout(&chunk[i], 2000)) {
                umcub_slot_abort(&w);
                ex_uart_putc('E');
                return;
            }
        }
        if (umcub_slot_write(&w, chunk, n) != 0) {
            umcub_slot_abort(&w);
            ex_uart_putc('E');
            return;
        }
        done += n;
        ex_uart_putc('K');
    }
    ex_uart_putc(umcub_slot_finish(&w, true, false) == 0 ? 'D' : 'E');
}

int main(void)
{
    umcub_set_node_address(APP_NODE_ADDR);   /* address on a shared bus, used by the bootloader */
    ex_led_init('B', 0);
    ex_uart_init();
    show_info();
    ex_puts("keys: i c b u r\n");

    uint32_t tick = 0;
    for (;;) {
        uint8_t c;
        if (ex_uart_getc(&c)) {
            switch (c) {
            case 'i': show_info(); break;
            case 'c': ex_puts(umcub_confirm() == 0 ? "confirmed\n" : "confirm failed\n"); break;
            case 'b': ex_puts("-> bootloader\n"); ex_delay_ms(5); umcub_enter_bootloader(0x1234); break;
            case 'u': upload(); break;
            case 'r': NVIC_SystemReset(); break;
            default: break;
            }
        }
        /* Feed IWDG1 in case the bootloader started it (UMCUB_CFG_WATCHDOG_MS):
         * once running it cannot be stopped. Harmless when it is not running. */
        IWDG1->KR = 0xAAAAu;
        ex_delay_ms(1);
        if (++tick == 250) {
            tick = 0;
            ex_led_toggle('B', 0);
        }
    }
}
