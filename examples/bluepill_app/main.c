/*
 * umcub example application, Blue Pill (STM32F103C8T6).
 *
 * Runs on the reset clock the bootloader leaves behind (HSI 8 MHz), blinks
 * the LED on PC13 and talks on USART1 PA9/PA10 (115200), the bootloader's
 * port:
 *   i  show bootloader / image information
 *   b  reboot into the bootloader's recovery mode
 *   u  receive an image and write it into the secondary slot (tools/app_upload.py)
 *   r  reset
 */
#include "stm32f1xx.h"
#include "umcub.h"

static const char *const reasons[] = { "normal", "upgraded", "reverted", "after recovery" };
static const char *const transports[] = { "-", "uart", "usb-cdc", "usb-dfu", "can", "eth", "app", "user" };

static volatile uint32_t ms;
void SysTick_Handler(void) { ms++; }
uint32_t umcub_port_millis(void) { return ms; }   /* timeouts of the flash driver */

static void putc_(char c)
{
    while (!(USART1->SR & USART_SR_TXE)) {
    }
    USART1->DR = (uint8_t)c;
}

static void puts_(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            putc_('\r');
        }
        putc_(*s++);
    }
}

static void put_u32(uint32_t v)
{
    char b[11];
    int i = 10;
    b[i] = 0;
    do {
        b[--i] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    puts_(&b[i]);
}

static void put_hex(uint32_t v)
{
    puts_("0x");
    for (int s = 28; s >= 0; s -= 4) {
        putc_("0123456789abcdef"[(v >> s) & 0xFu]);
    }
}

static bool getc_(uint8_t *c)
{
    uint32_t sr = USART1->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
        *c = (uint8_t)USART1->DR;
        return (sr & USART_SR_RXNE) != 0;
    }
    return false;
}

static bool getc_timeout(uint8_t *c, uint32_t timeout)
{
    uint32_t start = ms;
    while ((uint32_t)(ms - start) < timeout) {
        if (getc_(c)) {
            return true;
        }
    }
    return false;
}

static void put_ver(const umcub_version_t *v)
{
    put_u32(v->major);
    putc_('.');
    put_u32(v->minor);
    putc_('.');
    put_u32(v->revision);
    putc_('+');
    put_u32(v->build);
}

static void show_info(void)
{
    umcub_version_t v;
    puts_("\napp " APP_VERSION_STR " (Blue Pill)\n");
    if (umcub_boot_version(&v) == 0) {
        puts_("bootloader: ");
        put_ver(&v);
        puts_("\n");
    }
    const umcub_handoff_t *h = umcub_boot_info();
    if (!h) {
        puts_("no handoff data (started without bootloader?)\n");
        return;
    }
    puts_("boot reason: ");
    puts_(h->boot_reason < 4 ? reasons[h->boot_reason] : "?");
    puts_(", last update via ");
    puts_(h->last_transport < 8 ? transports[h->last_transport] : "?");
    puts_(", reset cause ");
    put_u32(h->reset_cause);
    puts_("\nimage 0: ");
    put_ver(&h->image_version[0]);
    puts_(" @ ");
    put_hex(h->image_addr[0]);
    puts_("\n");
}

/* 'u' <image:u8> <size:u32 le> then chunks of up to 1024 bytes; every chunk
 * acknowledged with 'K' (or 'E'), final 'D' (done) or 'E'. */
static void upload(void)
{
    static uint8_t chunk[1024];
    uint8_t hdr[5];
    for (int i = 0; i < 5; i++) {
        if (!getc_timeout(&hdr[i], 2000)) {
            putc_('E');
            return;
        }
    }
    uint32_t size = hdr[1] | (uint32_t)hdr[2] << 8 | (uint32_t)hdr[3] << 16 | (uint32_t)hdr[4] << 24;
    umcub_slot_writer_t w;
    if (umcub_slot_begin(&w, hdr[0], UMCUB_SLOT_DEFAULT, size) != 0) {
        putc_('E');
        return;
    }
    putc_('K');
    for (uint32_t done = 0; done < size;) {
        uint32_t n = size - done > sizeof(chunk) ? sizeof(chunk) : size - done;
        for (uint32_t i = 0; i < n; i++) {
            if (!getc_timeout(&chunk[i], 2000)) {
                umcub_slot_abort(&w);
                putc_('E');
                return;
            }
        }
        if (umcub_slot_write(&w, chunk, n) != 0) {
            umcub_slot_abort(&w);
            putc_('E');
            return;
        }
        done += n;
        putc_('K');
    }
    putc_(umcub_slot_finish(&w, true, false) == 0 ? 'D' : 'E');
}

int main(void)
{
    /* HSI 8 MHz (reset clock, the bootloader restored it). */
    SystemCoreClock = 8000000u;
    SysTick_Config(SystemCoreClock / 1000u);

    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPCEN | RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;
    GPIOC->CRH = (GPIOC->CRH & ~(0xFu << 20)) | (0x2u << 20);      /* PC13 push-pull 2 MHz */
    GPIOA->CRH = (GPIOA->CRH & ~(0xFFu << 4)) | (0x4Bu << 4);      /* PA9 AF PP 50 MHz, PA10 input */
    USART1->BRR = (8000000u + 115200u / 2u) / 115200u;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    show_info();
    puts_("keys: i b u r\n");

    uint32_t last = 0;
    for (;;) {
        uint8_t c;
        if (getc_(&c)) {
            switch (c) {
            case 'i': show_info(); break;
            case 'b': puts_("-> bootloader\n"); umcub_enter_bootloader(0x1234); break;
            case 'u': upload(); break;
            case 'r': NVIC_SystemReset(); break;
            default: break;
            }
        }
        IWDG->KR = 0xAAAAu;     /* in case the bootloader started the IWDG */
        if ((uint32_t)(ms - last) >= 250u) {
            last = ms;
            GPIOC->ODR ^= 1u << 13;
        }
    }
}

/* Linked with -nostartfiles: __libc_init_array still calls these. */
void _init(void) {}
void _fini(void) {}
