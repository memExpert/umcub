/*
 * List of compiled-in transports, in polling order.
 */
#include "umcub_cfg.h"
#include "umcub_transport.h"

extern const umcub_transport_t umcub_transport_uart;
extern const umcub_transport_t umcub_transport_usb;
extern const umcub_transport_t umcub_transport_can;
extern const umcub_transport_t umcub_transport_eth;
extern const umcub_transport_t umcub_transport_user;   /* boards/<b>/umcub_board.c */

const umcub_transport_t *const umcub_transports[] = {
#if UMCUB_CFG_TRANSPORT_UART
    &umcub_transport_uart,
#endif
#if UMCUB_CFG_USB
    &umcub_transport_usb,
#endif
#if UMCUB_CFG_TRANSPORT_CAN
    &umcub_transport_can,
#endif
#if UMCUB_CFG_TRANSPORT_ETH
    &umcub_transport_eth,
#endif
#if UMCUB_CFG_TRANSPORT_USER
    &umcub_transport_user,
#endif
    0,
};
_Static_assert(sizeof(umcub_transports) / sizeof(umcub_transports[0]) - 1u <= UMCUB_TRANSPORT_MAX,
               "UMCUB_TRANSPORT_MAX");

const unsigned umcub_transport_count = sizeof(umcub_transports) / sizeof(umcub_transports[0]) - 1u;

void umcub_transports_init(void)
{
    for (unsigned i = 0; i < umcub_transport_count; i++) {
        umcub_transports[i]->init();
    }
}

void umcub_transports_deinit(void)
{
    for (unsigned i = umcub_transport_count; i-- > 0;) {
        umcub_transports[i]->deinit();
    }
}

void umcub_transports_poll(void)
{
    for (unsigned i = 0; i < umcub_transport_count; i++) {
        if (umcub_transports[i]->poll) {
            umcub_transports[i]->poll();
        }
    }
}
