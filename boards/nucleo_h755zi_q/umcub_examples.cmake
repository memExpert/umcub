# Example applications of this board (UMCUB_BUILD_EXAMPLES), read by the top-level CMakeLists.txt:
# directories under examples/ (or absolute paths), built in the bootloader's tree.
if(UMCUB_CORE STREQUAL "cm7")
  list(APPEND UMCUB_BOARD_EXAMPLES h755_cm7_app)
endif()
# SINGLE_BOOT: the CM7 bootloader also boots the CM4 image; PER_CORE: the CM4 one does.
if((UMCUB_CORE STREQUAL "cm7" AND UMCUB_CFG_DUALCORE_MODE EQUAL 1) OR UMCUB_CORE STREQUAL "cm4")
  list(APPEND UMCUB_BOARD_EXAMPLES h755_cm4_app)
endif()
