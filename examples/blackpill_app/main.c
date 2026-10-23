/*
 * umcub example application, Black Pill (STM32F411CEU6).
 *
 * Starts on the reset clock the bootloader leaves behind (HSI 16 MHz) and
 * switches to the 25 MHz crystal: HSI is only +-4 % over temperature (DS10314,
 * more after soldering), too far off for 115200 baud. Blinks the LED on PC13
 * and talks on USART1 PA9/PA10 (115200), the bootloader's
 * port; PA8 is driven high while sending (DE of an RS485 transceiver, no
 * effect on a plain UART):
 *   i  show bootloader / image information
 *   b  reboot into the bootloader's recovery mode
 *   u  receive an image and write it into the secondary slot (tools/host/app_upload.py)
 *   w  stop feeding the watchdog (UMCUB_CFG_WATCHDOG_MS: reset follows)
 *   r  reset
 */
#include "stm32f4xx.h"
#include "umcub.h"

static const char *const reasons[] = { "normal", "upgraded", "reverted", "after recovery" };
static const char *const transports[] = { "-", "uart", "usb-cdc", "usb-dfu", "can", "eth", "app", "user" };

static volatile uint32_t ms;
void SysTick_Handler(void) { ms++; }
uint32_t umcub_port_millis(void) { return ms; }   /* timeouts of the flash driver */

static void putc_(char c)
{
    GPIOA->BSRR = 1u << 8;                  /* DE: transmit */
    USART1->DR = (uint8_t)c;
    while (!(USART1->SR & USART_SR_TC)) {
    }
    GPIOA->BSRR = 1u << (8 + 16);           /* DE: receive */
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
    puts_("\napp " APP_VERSION_STR " (Black Pill F411)\n");
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
    puts_("\nboard type ");
    put_hex(h->board_type);
    puts_(" rev ");
    put_u32(h->board_rev);
    puts_(", node address ");
    put_u32(h->node_addr);
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
    umcub_set_node_address(APP_NODE_ADDR);   /* address on a shared bus, used by the bootloader */
    /* HSE 25 MHz as SYSCLK (no PLL, 0 wait states up to 30 MHz); stays on
     * HSI 16 MHz, the reset clock the bootloader restored, without a crystal. */
    SystemCoreClock = 16000000u;
    RCC->CR |= RCC_CR_HSEON;
    for (uint32_t n = 0; n < 500000u; n++) {
        if (RCC->CR & RCC_CR_HSERDY) {
            RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSE;
            while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSE) {
            }
            SystemCoreClock = 25000000u;
            break;
        }
    }
    SysTick_Config(SystemCoreClock / 1000u);

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;
    GPIOC->MODER = (GPIOC->MODER & ~(3u << 26)) | (1u << 26);       /* PC13 output */
    GPIOA->BSRR = 1u << (8 + 16);
    GPIOA->MODER = (GPIOA->MODER & ~(0x3Fu << 16)) | (0x29u << 16); /* PA8 output, PA9/PA10 AF */
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~0xFF0u) | 0x770u;              /* PA9/PA10 AF7 = USART1 */
    GPIOA->PUPDR = (GPIOA->PUPDR & ~(3u << 20)) | (1u << 20);       /* PA10 pull-up */
    USART1->BRR = (SystemCoreClock + 115200u / 2u) / 115200u;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    show_info();
    puts_("keys: i b u w r\n");

    uint32_t last = 0;
    bool feed = true;
    for (;;) {
        uint8_t c;
        if (getc_(&c)) {
            switch (c) {
            case 'i': show_info(); break;
            case 'b': puts_("-> bootloader\n"); umcub_enter_bootloader(0x1234); break;
            case 'u': upload(); break;
            case 'w': puts_("watchdog: not fed any more\n"); feed = false; break;
            case 'r': NVIC_SystemReset(); break;
            default: break;
            }
        }
        if (feed) {
            IWDG->KR = 0xAAAAu; /* in case the bootloader started the IWDG (it cannot be stopped) */
        }
        if ((uint32_t)(ms - last) >= 250u) {
            last = ms;
            GPIOC->ODR ^= 1u << 13;
        }
    }
}

/* Linked with -nostartfiles: __libc_init_array still calls these. */
void _init(void) {}
void _fini(void) {}
