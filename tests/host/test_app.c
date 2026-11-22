/*
 * The application library (umcub::app, UMCUB_BUILDING_APP) on two images, against the handoff a bootloader
 * published: updates of both images in one application run keep the other core's running slot out of reach.
 * The handoff lives at UMCUB_CFG_SHARED_RAM_ADDR, mapped here at that very address.
 */
#define UMCUB_BUILDING_APP 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "fake_port.h"
#include "../../src/image/umcub.c"
#include "../../src/image/slot_writer.c"
#include "../../src/image/mcuboot/flash_map_backend.c"
#include "../../third_party/mcuboot/boot/bootutil/src/bootutil_public.c"

static int failures;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
            failures++;                                                  \
        }                                                                \
    } while (0)

static uint8_t *load(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(2);
    }
    static uint8_t buf[2][512 * 1024];
    static int n;
    *len = fread(buf[n], 1, sizeof(buf[n]), f);
    fclose(f);
    return buf[n++];
}

/* What the bootloader publishes before the jump: image 0 (this core) and image 1 (the other core) run from their
 * primary slots. */
static void publish(void)
{
    umcub_handoff_t *h = (umcub_handoff_t *)UMCUB_CFG_SHARED_RAM_ADDR;
    memset(h, 0, sizeof(*h));
    h->magic = UMCUB_HANDOFF_MAGIC;
    h->version = UMCUB_HANDOFF_VERSION;
    h->size = sizeof(*h);
    h->image_count = 2;
    h->image_addr[0] = UMCUB_CFG_IMG0_PRIMARY_ADDR;
    h->image_addr[1] = UMCUB_CFG_IMG1_PRIMARY_ADDR;
    h->crc32 = umcub_crc32((const uint8_t *)h + UMCUB_HANDOFF_INFO_OFFSET, UMCUB_HANDOFF_INFO_LEN);
}

static int update(int image, int slot, const uint8_t *img, size_t len, int *slot_used)
{
    umcub_slot_writer_t w;
    int rc = umcub_slot_begin(&w, image, slot, (uint32_t)len);
    if (rc) {
        return rc;
    }
    *slot_used = w.slot;
    rc = umcub_slot_write(&w, img, len);
    return rc ? rc : umcub_slot_finish(&w, true, false);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s image0.signed.bin image1.signed.bin\n", argv[0]);
        return 2;
    }
    size_t n0, n1;
    uint8_t *i0 = load(argv[1], &n0), *i1 = load(argv[2], &n1);
    uintptr_t page = UMCUB_CFG_SHARED_RAM_ADDR & ~(uintptr_t)0xFFF;
    if (mmap((void *)page, 0x2000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) !=
        (void *)page) {
        perror("mmap");
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    int slot;

    printf("[app, two images] update image 0, then image 1 in the same run\n");
    fake_flash_reset();
    publish();
    CHECK(umcub_boot_info() != NULL);
    CHECK(update(0, UMCUB_SLOT_SECONDARY, i0, n0, &slot) == 0 && slot == 1);
    const umcub_handoff_t *h = umcub_boot_info();
    CHECK(h != NULL && h->last_transport == UMCUB_TRANSPORT_APP);  /* still valid after the note */
    CHECK(update(1, UMCUB_SLOT_INACTIVE, i1, n1, &slot) == 0 && slot == 1);  /* the other core runs from 0 */
    umcub_slot_writer_t w;
    CHECK(umcub_slot_begin(&w, 1, UMCUB_SLOT_PRIMARY, (uint32_t)n1) == UMCUB_EBUSY);

    printf("[app, two images] no valid handoff: every primary slot may be running\n");
    fake_flash_reset();
    publish();
    ((umcub_handoff_t *)UMCUB_CFG_SHARED_RAM_ADDR)->crc32 ^= 1u;
    CHECK(umcub_boot_info() == NULL);
    CHECK(umcub_slot_begin(&w, 1, UMCUB_SLOT_PRIMARY, (uint32_t)n1) == UMCUB_EBUSY);
    CHECK(update(1, UMCUB_SLOT_INACTIVE, i1, n1, &slot) == 0 && slot == 1);

    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
