/*
 * Application linker script, STM32H7 CM7: everything after the MEMORY block, which
 * comes from the slot's fragment (umcub_app_img<n>_slot<m>.ld, `umcub image ld`;
 * umcub_app_linker_script() includes both). Needs -L port/arch/cortexm.
 */
REGION_ALIAS("RAM", RAM_D1);           /* CubeMX name of the AXI SRAM */
_Min_Heap_Size  = 0x200;
_Min_Stack_Size = 0x400;
_estack = ORIGIN(RAM) + LENGTH(RAM);

INCLUDE cortexm_sections.ld.inc
