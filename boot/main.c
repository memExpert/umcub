/*
 * umcub bootloader main flow:
 *
 *   port init -> entry decision (app request / GPIO / wait window)
 *     -> recovery mode (SMP over all transports)     [if requested]
 *     -> MCUboot boot_go(): verify, install/revert updates
 *     -> handoff info -> release second core -> deinit -> jump
 */
#include <string.h>
#include "umcub_cfg.h"
#include "umcub_boot.h"
#include "umcub_log.h"
#include "umcub_port.h"
#include "umcub_transport.h"
#include "umcub_version.h"
#include "bootutil/bootutil.h"
#include "bootutil/image.h"
#include "bootutil/fault_injection_hardening.h"
#include "bootutil/bootutil_public.h"
#include "bootutil_priv.h"   /* loader state: active slot per image (direct-xip) */
#include "flash_map_backend/flash_map_backend.h"
#include "sysflash/sysflash.h"

#if UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_DIRECT_XIP || UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_DIRECT_XIP_REVERT
#define DIRECT_XIP 1
#else
#define DIRECT_XIP 0
#endif

#if UMCUB_CFG_DUALCORE_MODE == UMCUB_DUALCORE_PER_CORE && defined(UMCUB_CORE_CM4)
#define IS_SECOND_CORE_INSTANCE 1
#else
#define IS_SECOND_CORE_INSTANCE 0
#endif
#if UMCUB_CFG_DUALCORE_MODE == UMCUB_DUALCORE_PER_CORE && !defined(UMCUB_CORE_CM4)
#define IS_MAIN_PER_CORE_INSTANCE 1
#else
#define IS_MAIN_PER_CORE_INSTANCE 0
#endif

#ifndef UMCUB_CFG_CORE2_DONE_TIMEOUT_MS
#define UMCUB_CFG_CORE2_DONE_TIMEOUT_MS 3000
#endif

static const char *const mode_names[] __attribute__((unused)) = {
    "?", "overwrite", "swap-scratch", "swap-move", "swap-offset", "direct-xip", "direct-xip-revert",
};

static __attribute__((unused)) bool read_header(uint32_t addr, struct image_header *hdr)
{
    return umcub_flash_read(addr, hdr, sizeof(*hdr)) == 0 && hdr->ih_magic == IMAGE_MAGIC;
}

static void version_from_header(const struct image_header *hdr, umcub_version_t *v)
{
    v->major = hdr->ih_ver.iv_major;
    v->minor = hdr->ih_ver.iv_minor;
    v->revision = hdr->ih_ver.iv_revision;
    v->build = hdr->ih_ver.iv_build_num;
}

static bool entry_gpio_active(void)
{
#if UMCUB_CFG_ENTRY_GPIO
    umcub_port_gpio_input(UMCUB_CFG_ENTRY_GPIO_PIN, UMCUB_CFG_ENTRY_GPIO_PULL);
    umcub_port_delay_ms(2);
    bool level = umcub_port_gpio_read(UMCUB_CFG_ENTRY_GPIO_PIN);
    umcub_port_gpio_reset(UMCUB_CFG_ENTRY_GPIO_PIN);
    return level == (UMCUB_CFG_ENTRY_GPIO_ACTIVE != 0);
#else
    return false;
#endif
}

static __attribute__((noreturn)) void recovery(const char *why)
{
    UMCUB_LOG_INF("entering recovery: %s", why);
    umcub_transports_init();
    umcub_recovery_run();
}

/* Start address of the image that boot_go() selected (image 0). */
static uint32_t image_vtor(const struct boot_rsp *rsp)
{
    return rsp->br_image_off + rsp->br_hdr->ih_hdr_size;
}

int main(void)
{
#if IS_SECOND_CORE_INSTANCE
    /* Clocks and power are owned by the main core's bootloader. */
    (void)umcub_port_core_wait(UMCUB_CORE_EVT_CLOCKS_READY, 0);
#endif
    umcub_port_init();
#if IS_MAIN_PER_CORE_INSTANCE
    umcub_port_core_signal(UMCUB_CORE_EVT_CLOCKS_READY);
#endif
#if UMCUB_CFG_WATCHDOG_MS
    umcub_port_wdg_start(UMCUB_CFG_WATCHDOG_MS);
#endif

    /* Bring up the log channel early (no-op without a UART transport). */
#if UMCUB_CFG_TRANSPORT_UART
    extern const umcub_transport_t umcub_transport_uart;
    umcub_transport_uart.init();
#endif
    UMCUB_LOG_INF("umcub %d.%d.%d (%08lx) %s, %s, %d image(s)",
                  UMCUB_VERSION_MAJOR, UMCUB_VERSION_MINOR, UMCUB_VERSION_PATCH,
                  (unsigned long)UMCUB_GIT_HASH, UMCUB_BOARD_NAME,
                  mode_names[UMCUB_CFG_UPGRADE_MODE], UMCUB_CFG_IMAGE_NUMBER);

    uint32_t req_arg = 0;
    uint32_t request = umcub_handoff_take_request(&req_arg);
#if UMCUB_CFG_ENTRY_ON_REQUEST
    if (request == UMCUB_REQ_ENTER_BOOT) {
        UMCUB_LOG_INF("application request (arg 0x%08lx)", (unsigned long)req_arg);
        recovery("requested by application");
    }
#endif
    /* "boot app" text command from recovery mode: don't fall back into
     * recovery because the entry pin is still held or the window is open. */
    bool skip_entry = request == UMCUB_REQ_BOOT_APP;
    if (!skip_entry && entry_gpio_active()) {
        recovery("entry pin");
    }
#if UMCUB_CFG_ENTRY_WAIT_MS > 0
    if (!skip_entry) {
        umcub_transports_init();
        if (umcub_recovery_wait(UMCUB_CFG_ENTRY_WAIT_MS)) {
            recovery("host request during wait window");
        }
        umcub_transports_deinit();
#if UMCUB_CFG_TRANSPORT_UART
        umcub_transport_uart.init();
#endif
    }
#endif

    umcub_handoff_t info;
    memset(&info, 0, sizeof(info));
    info.boot_version.major = UMCUB_VERSION_MAJOR;
    info.boot_version.minor = UMCUB_VERSION_MINOR;
    info.boot_version.revision = UMCUB_VERSION_PATCH;
    info.boot_git_hash = UMCUB_GIT_HASH;
    info.reset_cause = umcub_port_reset_cause();
    info.reset_flags = umcub_port_reset_flags();
    info.last_transport = umcub_handoff_last_transport();
    info.image_count = UMCUB_CFG_IMAGE_NUMBER;
    info.upgrade_mode = UMCUB_CFG_UPGRADE_MODE;

    /* What MCUboot is about to do with image 0 (swap modes). */
#if !DIRECT_XIP
    int swap = boot_swap_type_multi(0);
    info.boot_reason = (swap == BOOT_SWAP_TYPE_TEST || swap == BOOT_SWAP_TYPE_PERM) ? UMCUB_BOOT_UPGRADED :
                       swap == BOOT_SWAP_TYPE_REVERT ? UMCUB_BOOT_REVERTED : UMCUB_BOOT_NORMAL;
#endif

    struct boot_rsp rsp;
    FIH_DECLARE(fih_rc, FIH_FAILURE);
    bool core2_ok = UMCUB_CFG_IMAGE_NUMBER > 1 && UMCUB_CFG_DUALCORE_MODE == UMCUB_DUALCORE_SINGLE_BOOT;
    FIH_CALL(boot_go, fih_rc, &rsp);
    if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
#if UMCUB_CFG_IMAGE_NUMBER > 1 && !DIRECT_XIP
        /* Second image broken or missing: still start the first one.
         * (Not in direct-xip(-revert): a second loader pass would scramble
         * slots the first pass just marked.) */
        UMCUB_LOG_WRN("boot_go failed, trying image 0 alone");
        core2_ok = false;
        boot_state_init(boot_get_loader_state());
        FIH_CALL(boot_go_for_image_id, fih_rc, &rsp, 0);
#endif
        if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
            UMCUB_LOG_ERR("no bootable image");
#if UMCUB_CFG_ENTRY_ON_NO_IMAGE
            recovery("no bootable image");
#else
            umcub_port_delay_ms(1000);
            umcub_port_reset();
#endif
        }
    }

    uint32_t vtor0 = image_vtor(&rsp);
    version_from_header(rsp.br_hdr, &info.image_version[0]);
    info.image_addr[0] = rsp.br_image_off;
    info.active_slot[0] = rsp.br_image_off == UMCUB_CFG_IMG0_PRIMARY_ADDR ? 0 : 1;

    uint32_t vtor1 = 0;
    (void)core2_ok;
#if UMCUB_CFG_IMAGE_NUMBER > 1
    if (core2_ok) {
#if DIRECT_XIP
        /* boot_go() already chose and validated image 1's slot. */
        uint32_t slot = boot_get_loader_state()->slot_usage[1].active_slot;
        uint32_t base = slot == 0 ? UMCUB_CFG_IMG1_PRIMARY_ADDR : UMCUB_CFG_IMG1_SECONDARY_ADDR;
        info.active_slot[1] = (uint8_t)slot;
#else
        uint32_t base = UMCUB_CFG_IMG1_PRIMARY_ADDR;
#endif
        struct image_header hdr;
        if (read_header(base, &hdr)) {
            vtor1 = base + hdr.ih_hdr_size;
            version_from_header(&hdr, &info.image_version[1]);
            info.image_addr[1] = base;
        }
    }
#endif

    UMCUB_LOG_INF("image 0: %d.%d.%d+%lu @ 0x%08lx%s", info.image_version[0].major,
                  info.image_version[0].minor, info.image_version[0].revision,
                  (unsigned long)info.image_version[0].build, (unsigned long)vtor0,
                  info.boot_reason == UMCUB_BOOT_UPGRADED ? " (upgraded)" :
                  info.boot_reason == UMCUB_BOOT_REVERTED ? " (reverted)" : "");
    if (vtor1) {
        UMCUB_LOG_INF("image 1: %d.%d.%d+%lu @ 0x%08lx", info.image_version[1].major,
                      info.image_version[1].minor, info.image_version[1].revision,
                      (unsigned long)info.image_version[1].build, (unsigned long)vtor1);
    }

    umcub_handoff_publish(&info);

#if UMCUB_CFG_DUALCORE_MODE == UMCUB_DUALCORE_SINGLE_BOOT
    if (!vtor1) {
        UMCUB_LOG_WRN("second core stays parked (no valid image 1)");
    }
#elif IS_MAIN_PER_CORE_INSTANCE
    /* Keep clocks for the second core's bootloader until it is done. */
    if (!umcub_port_core_wait(UMCUB_CORE_EVT_BOOT_DONE, UMCUB_CFG_CORE2_DONE_TIMEOUT_MS)) {
        UMCUB_LOG_WRN("second core bootloader not done: leaving clocks running");
        umcub_port_keep_clocks(true);
    }
#endif

    UMCUB_LOG_INF("jump to 0x%08lx", (unsigned long)vtor0);
    umcub_transports_deinit();
#if IS_SECOND_CORE_INSTANCE
    umcub_port_core_signal(UMCUB_CORE_EVT_BOOT_DONE);
#endif
    umcub_port_deinit();
#if UMCUB_CFG_DUALCORE_MODE == UMCUB_DUALCORE_SINGLE_BOOT
    /* Release the second core only now: it starts on the reset clock tree,
     * exactly like after a power-on without bootloader. */
    if (vtor1) {
        umcub_port_core2_release(vtor1);
    }
#endif
    umcub_port_jump(vtor0);
}
