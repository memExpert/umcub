/* Overlay (POST): EXPERIMENTAL link-time optimisation (UMCUB_CFG_LTO), see the
 * template - 3-4 K smaller on Cortex-M3, about 6 K on the H7. */
#undef UMCUB_CFG_LTO
#define UMCUB_CFG_LTO                   1
