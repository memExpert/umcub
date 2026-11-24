/* The board hook of test_node.c, in its own file: state.c defines a weak default in that one. */
#include <stdbool.h>
#include <stdint.h>

extern int board_addr;

bool umcub_board_node_address(uint16_t *addr)
{
    if (board_addr < 0) {
        return false;
    }
    *addr = (uint16_t)board_addr;
    return true;
}
