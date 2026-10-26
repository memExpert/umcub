/* Overlay (POST): no SMP - images through the lite upload protocol
 * (tools/host/umcub_lite.py), USB DFU or the application. About 3 K smaller. */
#undef UMCUB_CFG_SMP_ENABLE
#define UMCUB_CFG_SMP_ENABLE            0
