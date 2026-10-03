/*
 * MCUboot flash map backend on top of the umcub port flash driver.
 * Used by the bootloader and by the application library.
 */
#include <errno.h>
#include <string.h>
#include "umcub_cfg.h"
#include "umcub_port.h"
#include "flash_map_backend/flash_map_backend.h"
#include "sysflash/sysflash.h"

static const struct flash_area areas[] = {
    { FLASH_AREA_BOOTLOADER, 0, 0, UMCUB_CFG_BOOT_ADDR, UMCUB_CFG_BOOT_SIZE },
    { FLASH_AREA_IMAGE_0, 0, 0, UMCUB_CFG_IMG0_PRIMARY_ADDR, UMCUB_CFG_IMG0_PRIMARY_SIZE },
    { FLASH_AREA_IMAGE_1, 0, 0, UMCUB_CFG_IMG0_SECONDARY_ADDR, UMCUB_CFG_IMG0_SECONDARY_SIZE },
#if UMCUB_CFG_IMAGE_NUMBER > 1
    { FLASH_AREA_IMAGE_2, 0, 0, UMCUB_CFG_IMG1_PRIMARY_ADDR, UMCUB_CFG_IMG1_PRIMARY_SIZE },
    { FLASH_AREA_IMAGE_3, 0, 0, UMCUB_CFG_IMG1_SECONDARY_ADDR, UMCUB_CFG_IMG1_SECONDARY_SIZE },
#endif
#if UMCUB_CFG_SCRATCH_SIZE > 0
    { FLASH_AREA_IMAGE_SCRATCH, 0, 0, UMCUB_CFG_SCRATCH_ADDR, UMCUB_CFG_SCRATCH_SIZE },
#endif
};

static const struct flash_area *find(uint8_t id)
{
    for (unsigned i = 0; i < sizeof(areas) / sizeof(areas[0]); i++) {
        if (areas[i].fa_id == id) {
            return &areas[i];
        }
    }
    return NULL;
}

const struct flash_area *umcub_flash_area_by_addr(uint32_t addr)
{
    for (unsigned i = 0; i < sizeof(areas) / sizeof(areas[0]); i++) {
        if (addr >= areas[i].fa_off && addr - areas[i].fa_off < areas[i].fa_size) {
            return &areas[i];
        }
    }
    return NULL;
}

static bool in_area(const struct flash_area *fa, uint32_t off, uint32_t len)
{
    return off <= fa->fa_size && len <= fa->fa_size - off;
}

int flash_area_open(uint8_t id, const struct flash_area **fa)
{
    *fa = find(id);
    return *fa ? 0 : -ENOENT;
}

void flash_area_close(const struct flash_area *fa)
{
    (void)fa;
}

int flash_area_read(const struct flash_area *fa, uint32_t off, void *dst, uint32_t len)
{
    if (!in_area(fa, off, len)) {
        return -EINVAL;
    }
    return umcub_flash_read(fa->fa_off + off, dst, len) ? -EIO : 0;
}

int flash_area_write(const struct flash_area *fa, uint32_t off, const void *src, uint32_t len)
{
    if (!in_area(fa, off, len) || fa->fa_id == FLASH_AREA_BOOTLOADER) {
        return -EINVAL;
    }
    return umcub_flash_write(fa->fa_off + off, src, len) ? -EIO : 0;
}

int flash_area_erase(const struct flash_area *fa, uint32_t off, uint32_t len)
{
    if (!in_area(fa, off, len) || fa->fa_id == FLASH_AREA_BOOTLOADER) {
        return -EINVAL;
    }
    return umcub_flash_erase(fa->fa_off + off, len) ? -EIO : 0;
}

uint32_t flash_area_align(const struct flash_area *fa)
{
    (void)fa;
    return umcub_flash_write_align();
}

uint8_t flash_area_erased_val(const struct flash_area *fa)
{
    (void)fa;
    return umcub_flash_erased_val();
}

int flash_area_get_sector(const struct flash_area *fa, off_t off, struct flash_sector *fs)
{
    uint32_t start, size;
    if (off < 0 || (uint32_t)off >= fa->fa_size ||
        umcub_flash_sector_info(fa->fa_off + (uint32_t)off, &start, &size) != 0) {
        return -EINVAL;
    }
    fs->fs_off = start - fa->fa_off;
    fs->fs_size = size;
    return 0;
}

int flash_area_sectors(const struct flash_area *fa, uint32_t *count, struct flash_sector *sectors)
{
    uint32_t max = *count, n = 0, addr = fa->fa_off;
    while (addr < fa->fa_off + fa->fa_size) {
        uint32_t start, size;
        if (umcub_flash_sector_info(addr, &start, &size) != 0) {
            return -EINVAL;
        }
        if (n >= max) {
            return -ENOMEM;
        }
        sectors[n].fs_off = start - fa->fa_off;
        sectors[n].fs_size = size;
        n++;
        addr = start + size;
    }
    *count = n;
    return 0;
}

int flash_area_get_sectors(int fa_id, uint32_t *count, struct flash_sector *sectors)
{
    const struct flash_area *fa = find((uint8_t)fa_id);
    return fa ? flash_area_sectors(fa, count, sectors) : -ENOENT;
}

int flash_area_id_from_multi_image_slot(int image_index, int slot)
{
    switch (slot) {
    case 0: return FLASH_AREA_IMAGE_PRIMARY(image_index);
    case 1: return FLASH_AREA_IMAGE_SECONDARY(image_index);
#if UMCUB_CFG_SCRATCH_SIZE > 0
    case 2: return FLASH_AREA_IMAGE_SCRATCH;
#endif
    default: return -EINVAL;
    }
}

int flash_area_id_from_image_slot(int slot)
{
    return flash_area_id_from_multi_image_slot(0, slot);
}

int flash_area_id_to_multi_image_slot(int image_index, int area_id)
{
    if (area_id == FLASH_AREA_IMAGE_PRIMARY(image_index)) {
        return 0;
    }
    if (area_id == FLASH_AREA_IMAGE_SECONDARY(image_index)) {
        return 1;
    }
    return -EINVAL;
}

/* MCUBOOT_SERIAL_DIRECT_IMAGE_UPLOAD (same numbering as MCUboot on Zephyr):
 * 0/1 image 0 primary, 2 image 0 secondary, 3 image 1 primary, 4 image 1 secondary. */
int flash_area_id_from_direct_image(int image_id)
{
    if (image_id <= 1) {
        return image_id < 0 ? -EINVAL : FLASH_AREA_IMAGE_PRIMARY(0);
    }
    if (image_id - 1 >= 2 * UMCUB_CFG_IMAGE_NUMBER) {
        return -EINVAL;
    }
    return flash_area_id_from_multi_image_slot((image_id - 1) / 2, (image_id - 1) % 2);
}
