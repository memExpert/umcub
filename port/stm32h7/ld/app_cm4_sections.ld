/*
 * Application linker script, STM32H7 CM4: everything after the MEMORY block, which
 * comes from the slot's fragment (umcub_app_img<n>_slot<m>.ld, `umcub image ld`;
 * umcub_app_linker_script() includes both). Needs -L port/arch/cortexm.
 */
_Min_Heap_Size  = 0x200;
_Min_Stack_Size = 0x400;
_estack = ORIGIN(RAM) + LENGTH(RAM);

INCLUDE cortexm_sections.ld.inc
