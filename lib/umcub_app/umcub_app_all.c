/*
 * umcub::app as one translation unit.
 *
 * The CMake target builds this file; an IDE project (STM32CubeIDE, Keil MDK,
 * IAR, ...) adds just this one source plus the include paths listed in the
 * README ("Using umcub from an IDE"). The family part comes from
 * port/<family>/include/umcub_family_app.inc, so the IDE project needs no
 * per-series source list.
 */
#define UMCUB_BUILDING_APP 1

#include "src/umcub.c"
#include "src/slot_writer.c"
#include "../../mcuboot_port/src/flash_map_backend.c"
#include "../../third_party/mcuboot/boot/bootutil/src/bootutil_public.c"
#include "umcub_family_app.inc"
