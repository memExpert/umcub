/* UMCUB_CONFIG_POST for the simulators without SMP (umcub_sim_lite,
 * umcub_sim_can_lite): lite upload protocol + text commands, for
 * tools/umcub_lite.py in tests/host/link_e2e.py. */
#undef UMCUB_CFG_LOG_LEVEL
#define UMCUB_CFG_LOG_LEVEL 0
#undef UMCUB_CFG_UART_LINK
#define UMCUB_CFG_UART_LINK UMCUB_LINK_PLAIN
#undef UMCUB_CFG_SMP_ENABLE
#define UMCUB_CFG_SMP_ENABLE 0
#undef UMCUB_CFG_INSPECT_VERIFY
#define UMCUB_CFG_INSPECT_VERIFY 1
#undef UMCUB_CFG_CMD_ENABLE
#undef UMCUB_CFG_CMD_TABLE
#define UMCUB_CFG_CMD_ENABLE 1
#define UMCUB_CFG_CMD_TABLE UMCUB_CMD("i", UMCUB_CMD_INFO) UMCUB_CMD("verify", UMCUB_CMD_VERIFY)
