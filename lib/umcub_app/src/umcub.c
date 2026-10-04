/*
 * umcub application library: boot info, bootloader entry, image state.
 */
#include <string.h>
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "umcub.h"
#include "bootutil/bootutil_public.h"
#include "bootutil/image.h"
#include "flash_map_backend/flash_map_backend.h"
#include "sysflash/sysflash.h"

#define HANDOFF ((volatile umcub_handoff_t *)UMCUB_CFG_SHARED_RAM_ADDR)

const umcub_handoff_t *umcub_boot_info(void)
{
    const umcub_handoff_t *h = (const umcub_handoff_t *)UMCUB_CFG_SHARED_RAM_ADDR;
    if (h->magic != UMCUB_HANDOFF_MAGIC || h->version != UMCUB_HANDOFF_VERSION ||
        h->crc32 != umcub_crc32((const uint8_t *)h + UMCUB_HANDOFF_INFO_OFFSET, UMCUB_HANDOFF_INFO_LEN)) {
        return NULL;
    }
    return h;
}

const umcub_info_block_t *umcub_boot_info_block(void)
{
    const umcub_info_block_t *b = (const umcub_info_block_t *)UMCUB_CFG_BOOT_INFO_ADDR;
    umcub_info_block_t copy;
    if (umcub_flash_read(UMCUB_CFG_BOOT_INFO_ADDR, &copy, sizeof(copy)) != 0 ||
        copy.magic != UMCUB_INFO_MAGIC || copy.version < 1) {
        return NULL;
    }
    return b;
}

int umcub_boot_version(umcub_version_t *v)
{
    const umcub_info_block_t *b = umcub_boot_info_block();
    if (b) {
        *v = b->boot_version;
        return 0;
    }
    const umcub_handoff_t *h = umcub_boot_info();
    if (h) {
        *v = h->boot_version;
        return 0;
    }
    return UMCUB_ENOTSUP;
}

static int slot_area(int image, int slot, const struct flash_area **fa)
{
    if (image < 0 || image >= UMCUB_CFG_IMAGE_NUMBER || slot < 0 || slot > 1) {
        return UMCUB_EINVAL;
    }
    return flash_area_open((uint8_t)flash_area_id_from_multi_image_slot(image, slot), fa) ? UMCUB_EINVAL : 0;
}

int umcub_image_version(int image, int slot, umcub_version_t *v)
{
    const struct flash_area *fa;
    struct image_header hdr;
    int rc = slot_area(image, slot, &fa);
    if (rc) {
        return rc;
    }
    if (flash_area_read(fa, 0, &hdr, sizeof(hdr)) || hdr.ih_magic != IMAGE_MAGIC) {
        return UMCUB_EIO;
    }
    v->major = hdr.ih_ver.iv_major;
    v->minor = hdr.ih_ver.iv_minor;
    v->revision = hdr.ih_ver.iv_revision;
    v->build = hdr.ih_ver.iv_build_num;
    return 0;
}

__attribute__((noreturn)) void umcub_enter_bootloader(uint32_t arg)
{
#ifndef UMCUB_HOST_TEST
    __asm volatile("cpsid i" ::: "memory");
#endif
    HANDOFF->request = UMCUB_REQ_ENTER_BOOT;
    HANDOFF->request_arg = arg;
    HANDOFF->request_check = ~UMCUB_REQUEST_MAGIC ^ UMCUB_REQ_ENTER_BOOT;
    HANDOFF->request_magic = UMCUB_REQUEST_MAGIC;
    umcub_port_reset();
}

void umcub_set_node_address(uint16_t addr)
{
    HANDOFF->node_addr_req = (uint32_t)addr | ((uint32_t)(uint16_t)~addr << 16);
    HANDOFF->node_magic = UMCUB_NODE_MAGIC;     /* 32-bit stores only (ECC SRAM) */
#ifndef UMCUB_HOST_TEST
    __asm volatile("dsb" ::: "memory");
#endif
}

#ifdef UMCUB_BUILDING_APP
/* The next boot reports "last update via app" (umcub_handoff_t.last_transport,
 * carried over the reset like the recovery mux does it). 32-bit store only:
 * ECC SRAM loses narrower writes on reset. */
void umcub_note_app_update(void)
{
    volatile uint32_t *w = (volatile uint32_t *)((uintptr_t)&HANDOFF->last_transport & ~3u);
    union {
        uint32_t word;
        uint8_t b[4];
    } u = { .word = *w };
    u.b[(uintptr_t)&HANDOFF->last_transport & 3u] = UMCUB_TRANSPORT_APP;
    *w = u.word;
    __asm volatile("dsb" ::: "memory");
}
#endif

/* Image the running code belongs to (by the address of this function). */
static int running_image(void)
{
#if UMCUB_CFG_IMAGE_NUMBER > 1
    uint32_t pc = (uint32_t)(uintptr_t)&running_image;
    if ((pc - UMCUB_CFG_IMG1_PRIMARY_ADDR) < UMCUB_CFG_IMG1_PRIMARY_SIZE ||
        (pc - UMCUB_CFG_IMG1_SECONDARY_ADDR) < UMCUB_CFG_IMG1_SECONDARY_SIZE) {
        return 1;
    }
#endif
    return 0;
}

int umcub_confirm(void)
{
    return umcub_confirm_image(running_image());
}

#if UMCUB_CFG_UPGRADE_MODE >= UMCUB_MODE_DIRECT_XIP
/* Slot (0/1) of `image` that holds the running code, or -1. */
static int running_slot(int image)
{
    uint32_t pc = (uint32_t)(uintptr_t)&running_slot;
    for (int slot = 0; slot < 2; slot++) {
        const struct flash_area *fa;
        if (slot_area(image, slot, &fa) == 0 && pc - fa->fa_off < fa->fa_size) {
            return slot;
        }
    }
    return -1;
}
#endif

int umcub_confirm_image(int image)
{
    if (image < 0 || image >= UMCUB_CFG_IMAGE_NUMBER) {
        return UMCUB_EINVAL;
    }
#if UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_OVERWRITE || UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_DIRECT_XIP
    return 0;   /* nothing to confirm in these modes */
#elif UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_DIRECT_XIP_REVERT
    const struct flash_area *fa;
    int slot = running_slot(image);
    if (slot < 0 || slot_area(image, slot, &fa)) {
        return UMCUB_EINVAL;   /* only the image this code belongs to can be confirmed */
    }
    return boot_set_next(fa, true, true) ? UMCUB_EIO : 0;
#else
    return boot_set_confirmed_multi(image) ? UMCUB_EIO : 0;
#endif
}

/* Mark the image in `slot` for the next boot (test or permanent). */
int umcub_mark_slot(int image, int slot, bool permanent)
{
#if UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_DIRECT_XIP_REVERT
    const struct flash_area *fa;
    if (slot_area(image, slot, &fa)) {
        return UMCUB_EINVAL;
    }
    return boot_set_next(fa, false, permanent) ? UMCUB_EIO : 0;
#elif UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_DIRECT_XIP
    (void)image;
    (void)slot;
    (void)permanent;
    return 0;   /* the newest valid image wins, no marking */
#else
    if (slot != 1) {
        return UMCUB_EINVAL;   /* swap/overwrite install from the secondary slot */
    }
    return boot_set_pending_multi(image, permanent) ? UMCUB_EIO : 0;
#endif
}

int umcub_is_confirmed(int image)
{
#if UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_OVERWRITE
    /* overwrite: the copy carries the update's trailer (image_ok unset), but
     * nothing can revert an installed image. */
    return image < 0 || image >= UMCUB_CFG_IMAGE_NUMBER ? UMCUB_EINVAL : 1;
#else
    const struct flash_area *fa;
    uint8_t image_ok;
    int slot = 0;
#if UMCUB_CFG_UPGRADE_MODE >= UMCUB_MODE_DIRECT_XIP
    if (running_slot(image) == 1) {
        slot = 1;
    }
#endif
    if (slot_area(image, slot, &fa)) {
        return UMCUB_EINVAL;
    }
    struct boot_swap_state st;
    if (boot_read_swap_state(fa, &st)) {
        return UMCUB_EIO;
    }
    /* No trailer (installed by overwrite / serial recovery / programmer):
     * nothing can revert it, so it counts as confirmed. */
    if (st.magic != BOOT_MAGIC_GOOD) {
        return 1;
    }
    image_ok = st.image_ok;
    return image_ok == BOOT_FLAG_SET;
#endif
}

int umcub_request_upgrade(int image, bool permanent)
{
    if (image < 0 || image >= UMCUB_CFG_IMAGE_NUMBER) {
        return UMCUB_EINVAL;
    }
#if UMCUB_CFG_UPGRADE_MODE >= UMCUB_MODE_DIRECT_XIP
    int run = running_slot(image);
    return umcub_mark_slot(image, run == 0 ? 1 : 0, permanent);
#else
    return umcub_mark_slot(image, 1, permanent);
#endif
}

/* --- weak hooks ------------------------------------------------------------ */

__attribute__((weak)) uint32_t umcub_port_millis(void)
{
    /* DWT cycle counter (Cortex-M3 and up). */
    volatile uint32_t *demcr = (volatile uint32_t *)0xE000EDFCu;
    volatile uint32_t *dwt_ctrl = (volatile uint32_t *)0xE0001000u;
    volatile uint32_t *dwt_cyccnt = (volatile uint32_t *)0xE0001004u;
    extern uint32_t SystemCoreClock;
    static uint32_t last, ms, rem;

    if (!(*dwt_ctrl & 1u)) {
        *demcr |= 1u << 24;
        *dwt_cyccnt = 0;
        *dwt_ctrl |= 1u;
        last = 0;
    }
    uint32_t now = *dwt_cyccnt;
    uint32_t per_ms = SystemCoreClock / 1000u;
    rem += now - last;
    last = now;
    if (per_ms) {
        ms += rem / per_ms;
        rem %= per_ms;
    }
    return ms;
}

#ifndef UMCUB_HOST_TEST
__attribute__((weak, noreturn)) void umcub_port_reset(void)
{
    /* NVIC_SystemReset() without depending on device headers. */
    volatile uint32_t *aircr = (volatile uint32_t *)0xE000ED0Cu;
    __asm volatile("dsb" ::: "memory");
    *aircr = (0x5FAu << 16) | (*aircr & (7u << 8)) | (1u << 2);
    __asm volatile("dsb" ::: "memory");
    for (;;) {
    }
}
#endif

__attribute__((weak)) void umcub_port_wdg_feed(void)
{
}

#ifdef UMCUB_BUILDING_APP
/* bootutil_public.c asserts; the bootloader has its own handler (compat.c). */
__attribute__((weak)) void umcub_assert_fail(const char *file, int line)
{
    (void)file;
    (void)line;
}
#endif
