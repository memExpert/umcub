/* Overlay (POST): no SMP - images through the lite upload protocol
 * (umcub upload --lite), USB DFU or the application. About 3 K smaller. */
#undef UMCUB_CFG_SMP_ENABLE
#define UMCUB_CFG_SMP_ENABLE            0
