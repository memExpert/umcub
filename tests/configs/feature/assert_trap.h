/* UMCUB_CONFIG_POST: MCUboot assertions trap (HardFault -> reset) instead of
 * logging file:line (UMCUB_CFG_ASSERT_TRAP). */
#undef UMCUB_CFG_ASSERT_TRAP
#define UMCUB_CFG_ASSERT_TRAP           1
