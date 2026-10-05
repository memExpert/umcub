/* Overlay (POST), Blue Pill: SMP over CAN (bxCAN, ISO-TP) on PB9/PB8 next to
 * the UART; needs a CAN transceiver (UMCUB_CFG_CAN_LOOPBACK 1 for a test
 * without one). Classic CAN only. */
#undef UMCUB_CFG_TRANSPORT_CAN
#define UMCUB_CFG_TRANSPORT_CAN         1
