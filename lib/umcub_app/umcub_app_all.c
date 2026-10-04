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
/* MCUboot's own source: its warnings (implicit fallthrough in the direct-xip
 * paths of boot_set_next()) are not ours to fix - keep -Werror projects building. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#include "../../third_party/mcuboot/boot/bootutil/src/bootutil_public.c"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include "umcub_family_app.inc"
