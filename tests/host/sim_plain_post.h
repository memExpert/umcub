/* UMCUB_CONFIG_POST for the plain simulator (umcub_sim_plain): standard SMP
 * serial on the bus, slot inspection with readback - for the host tools that
 * talk SMP directly (tools/umcub_inspect.py) in tests/host/link_e2e.py. */
#undef UMCUB_CFG_LOG_LEVEL
#define UMCUB_CFG_LOG_LEVEL 0
#undef UMCUB_CFG_UART_LINK
#define UMCUB_CFG_UART_LINK UMCUB_LINK_PLAIN
#undef UMCUB_CFG_INSPECT_VERIFY
#define UMCUB_CFG_INSPECT_VERIFY 1
#undef UMCUB_CFG_INSPECT_HASH
#define UMCUB_CFG_INSPECT_HASH 1
#undef UMCUB_CFG_READBACK
#define UMCUB_CFG_READBACK 1
