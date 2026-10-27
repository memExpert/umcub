/* Overlay (POST), Black Pill F411: the board's own upload protocol of
 * boards/blackpill_f411ce/umcub_board.c instead of SMP and lite (host side
 * boards/blackpill_f411ce/proto_upload.py); text commands stay. */
#undef UMCUB_CFG_SMP_ENABLE
#define UMCUB_CFG_SMP_ENABLE            0
#undef UMCUB_CFG_LITE_UPLOAD
#define UMCUB_CFG_LITE_UPLOAD           0
#undef UMCUB_CFG_PROTO_USER
#define UMCUB_CFG_PROTO_USER            1
