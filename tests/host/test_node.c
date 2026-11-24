/*
 * Node address sources of the bootloader (src/util/state.c), in order: the application's request in the handoff
 * RAM, the word at UMCUB_CFG_NODE_ADDR_FLASH, the board hook, then UMCUB_CFG_NODE_ADDR (the build's default).
 * 0 (unassigned) and 0xFFFF (broadcast) in a source mean "not set there": the next source decides.
 * Built twice: TEST_NODE_ADDR = 0 and 7.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "umcub_cfg.h"
#undef UMCUB_CFG_LINK_ANY
#define UMCUB_CFG_LINK_ANY 1
#undef UMCUB_CFG_NODE_ADDR_FLASH
#define UMCUB_CFG_NODE_ADDR_FLASH 0x08001000u
#undef UMCUB_CFG_NODE_ADDR
#define UMCUB_CFG_NODE_ADDR TEST_NODE_ADDR
#include "umcub.h"
#include "../../src/util/state.c"

static int failures;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
            failures++;                                                  \
        }                                                                \
    } while (0)

/* The flash word and the board hook (test_node_board.c: a strong definition beats state.c's weak one), per case. */
static uint32_t flash_word = 0xFFFFFFFFu;
int board_addr = -1;

int umcub_flash_read(uint32_t addr, void *dst, size_t len)
{
    if (addr != UMCUB_CFG_NODE_ADDR_FLASH || len != 4) {
        return UMCUB_EINVAL;
    }
    memcpy(dst, &flash_word, 4);
    return UMCUB_OK;
}

uint8_t umcub_port_reset_cause(void)
{
    return 0;
}

static uint16_t boot(int request, uint32_t flash, int board)
{
    memset((void *)HANDOFF, 0, sizeof(umcub_handoff_t));
    if (request >= 0) {
        HANDOFF->node_addr_req = UMCUB_NODE_ADDR_WORD((uint16_t)request);
        HANDOFF->node_magic = UMCUB_NODE_MAGIC;
    }
    flash_word = flash;
    board_addr = board;
    umcub_node_init();
    return umcub_node_address();
}

int main(void)
{
    uintptr_t page = UMCUB_CFG_SHARED_RAM_ADDR & ~(uintptr_t)0xFFF;
    if (mmap((void *)page, 0x2000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) !=
        (void *)page) {
        perror("mmap");
        return 2;
    }
    const uint16_t dflt = TEST_NODE_ADDR;
    const uint32_t none = 0xFFFFFFFFu;             /* erased flash */
    printf("[node address, build default %u]\n", dflt);

    CHECK(boot(-1, none, -1) == dflt);                                     /* nothing set: the build's default */
    CHECK(boot(9, UMCUB_NODE_ADDR_WORD(5), 3) == 9);                       /* the application first */
    CHECK(boot(-1, UMCUB_NODE_ADDR_WORD(5), 3) == 5);                      /* then the flash word */
    CHECK(boot(-1, none, 3) == 3);                                         /* then the board */
    CHECK(boot(-1, 0x12345678u, -1) == dflt);                              /* a word that does not check out */
    /* 0 and 0xFFFF are "not set here", wherever they come from */
    CHECK(boot(0, UMCUB_NODE_ADDR_WORD(5), 3) == 5);
    CHECK(boot(0xFFFF, UMCUB_NODE_ADDR_WORD(5), 3) == 5);
    CHECK(boot(0, UMCUB_NODE_ADDR_WORD(0), 3) == 3);
    CHECK(boot(-1, UMCUB_NODE_ADDR_WORD(0xFFFF), 0) == dflt);
    CHECK(boot(0, UMCUB_NODE_ADDR_WORD(0), 0xFFFF) == dflt);

    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
