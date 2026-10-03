/*
 * Build-matrix board: NUCLEO-H755ZI-Q with board-supplied CAN and Ethernet
 * drivers (UMCUB_DRIVER_BOARD) and a board transport (UMCUB_CFG_TRANSPORT_USER).
 * Compile/link check only - the drivers in umcub_board.c are stubs.
 */
#include "../../../boards/nucleo_h755zi_q/umcub_config.h"

#undef UMCUB_CFG_ETH_RMII_PINS
#define UMCUB_CFG_CAN_DRIVER            UMCUB_DRIVER_BOARD
#define UMCUB_CFG_ETH_DRIVER            UMCUB_DRIVER_BOARD
#define UMCUB_CFG_TRANSPORT_USER        1
