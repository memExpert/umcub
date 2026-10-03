/*
 * Writing an MCUboot image into a slot from the application.
 *
 * Sectors are erased progressively in front of the data; the sector holding
 * the MCUboot trailer is erased up front so a stale trailer from an earlier
 * update can never be mistaken for a new request.
 */
#include <string.h>
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "umcub.h"
#include "bootutil/bootutil_public.h"
#include "bootutil/image.h"
#include "flash_map_backend/flash_map_backend.h"

int umcub_mark_slot(int image, int slot, bool permanent);

static bool executing_from(uint32_t base, uint32_t size)
{
#ifdef UMCUB_HOST_TEST
    (void)base;
    (void)size;
    return false;
#else
    uint32_t pc = (uint32_t)(uintptr_t)&executing_from;
    uint32_t vtor = *(volatile uint32_t *)0xE000ED08u;   /* SCB->VTOR */
    return (pc - base) < size || (vtor - base) < size;
#endif
}

static int resolve_slot(int image, int slot)
{
    if (slot == UMCUB_SLOT_DEFAULT) {
        slot = UMCUB_CFG_APP_WRITE_SLOT;
    }
    if (slot == UMCUB_SLOT_INACTIVE) {
        const struct flash_area *pri;
        if (flash_area_open((uint8_t)flash_area_id_from_multi_image_slot(image, 0), &pri)) {
            return UMCUB_EINVAL;
        }
        slot = executing_from(pri->fa_off, pri->fa_size) ? UMCUB_SLOT_SECONDARY : UMCUB_SLOT_PRIMARY;
    }
    return slot == UMCUB_SLOT_PRIMARY ? 0 : slot == UMCUB_SLOT_SECONDARY ? 1 : UMCUB_EINVAL;
}

static int erase_sector_at(uint32_t addr, uint32_t *next)
{
    uint32_t start, size;
    int rc = umcub_flash_sector_info(addr, &start, &size);
    if (rc) {
        return rc;
    }
    umcub_port_wdg_feed();
    rc = umcub_flash_erase(start, size);
    *next = start + size;
    return rc;
}

int umcub_slot_begin(umcub_slot_writer_t *w, int image, int slot, uint32_t total_size)
{
    const struct flash_area *fa;
    memset(w, 0, sizeof(*w));
    if (image < 0 || image >= UMCUB_CFG_IMAGE_NUMBER) {
        return UMCUB_EINVAL;
    }
    int s = resolve_slot(image, slot);
    if (s < 0 || flash_area_open((uint8_t)flash_area_id_from_multi_image_slot(image, s), &fa)) {
        return UMCUB_EINVAL;
    }
    if (executing_from(fa->fa_off, fa->fa_size)) {
        return UMCUB_EBUSY;     /* would erase the running code */
    }
#if UMCUB_CFG_UPGRADE_MODE >= UMCUB_MODE_SWAP_SCRATCH && UMCUB_CFG_UPGRADE_MODE <= UMCUB_MODE_SWAP_OFFSET
    /* After a test swap the secondary slot holds the previous image - the
     * only way back. Confirm (or let it revert) before writing a new one. */
    if (s == 1 && umcub_is_confirmed(image) == 0) {
        return UMCUB_EBUSY;
    }
#endif
    if (total_size > fa->fa_size || umcub_flash_write_align() > sizeof(w->buf)) {
        return UMCUB_EINVAL;
    }
    w->base = fa->fa_off;
    w->size = fa->fa_size;
    w->image = image;
    w->slot = s;

    uint32_t start, size, next;
    int rc;
#if UMCUB_CFG_UPGRADE_MODE == UMCUB_MODE_SWAP_OFFSET
    /* swap-offset: the update image starts at the second sector of the
     * secondary slot; the first one must not hold a stale image header. */
    if (s == 1) {
        rc = erase_sector_at(w->base, &next);
        if (rc) {
            return rc;
        }
        w->size -= next - w->base;
        w->base = next;
        if (total_size > w->size) {
            return UMCUB_EINVAL;
        }
    }
#endif

    /* Trailer sector first. */
    rc = umcub_flash_sector_info(w->base + w->size - 1u, &start, &size);
    if (rc) {
        return rc;
    }
    w->last_sector = start - w->base;
    rc = erase_sector_at(start, &next);
    if (rc) {
        return rc;
    }
    w->erased_end = 0;
    return 0;
}

static int ensure_erased(umcub_slot_writer_t *w, uint32_t end)
{
    while (w->erased_end < end) {
        if (w->erased_end >= w->last_sector) {
            w->erased_end = w->size;    /* trailer sector erased in begin() */
            break;
        }
        uint32_t next;
        int rc = erase_sector_at(w->base + w->erased_end, &next);
        if (rc) {
            return rc;
        }
        w->erased_end = next - w->base;
    }
    return 0;
}

static int program(umcub_slot_writer_t *w, uint32_t off, const uint8_t *data, uint32_t len)
{
    int rc = ensure_erased(w, off + len);
    return rc ? rc : umcub_flash_write(w->base + off, data, len);
}

int umcub_slot_write(umcub_slot_writer_t *w, const void *data, size_t len)
{
    const uint8_t *p = data;
    uint32_t align = umcub_flash_write_align();
    if (!w->size || w->off + w->buf_len + len > w->size) {
        return UMCUB_EINVAL;
    }
    while (len) {
        if (w->buf_len || len < align) {
            size_t n = align - w->buf_len;
            if (n > len) {
                n = len;
            }
            memcpy(&w->buf[w->buf_len], p, n);
            w->buf_len += (uint8_t)n;
            p += n;
            len -= n;
            if (w->buf_len == align) {
                int rc = program(w, w->off, w->buf, align);
                if (rc) {
                    return rc;
                }
                w->off += align;
                w->buf_len = 0;
            }
            continue;
        }
        uint32_t n = (uint32_t)len - (uint32_t)len % align;
        int rc = program(w, w->off, p, n);
        if (rc) {
            return rc;
        }
        w->off += n;
        p += n;
        len -= n;
    }
    return 0;
}

int umcub_slot_finish(umcub_slot_writer_t *w, bool request_upgrade, bool permanent)
{
    uint32_t align = umcub_flash_write_align();
    if (!w->size) {
        return UMCUB_EINVAL;
    }
    if (w->buf_len) {
        memset(&w->buf[w->buf_len], umcub_flash_erased_val(), align - w->buf_len);
        int rc = program(w, w->off, w->buf, align);
        if (rc) {
            return rc;
        }
        w->off += align;
        w->buf_len = 0;
    }

    struct image_header hdr;
    if (umcub_flash_read(w->base, &hdr, sizeof(hdr)) || hdr.ih_magic != IMAGE_MAGIC ||
        (uint64_t)hdr.ih_hdr_size + hdr.ih_img_size > w->off) {
        return UMCUB_EIO;   /* not a (complete) MCUboot image */
    }
    if (request_upgrade) {
        return umcub_mark_slot(w->image, w->slot, permanent);
    }
    return 0;
}

int umcub_slot_abort(umcub_slot_writer_t *w)
{
    int rc = 0;
    if (w->size && w->erased_end) {
        rc = umcub_flash_erase(w->base, w->erased_end);   /* sector aligned */
    }
    memset(w, 0, sizeof(*w));
    return rc;
}
