/* Runs from AXI SRAM, loaded by GDB. */
MEMORY { RAM (rwx) : ORIGIN = 0x24000000, LENGTH = 16K }
ENTRY(ob_entry)
SECTIONS
{
  .text : { KEEP(*(.text.entry)) *(.text*) *(.rodata*) } >RAM
  .data : { *(.data*) } >RAM
  .bss  : { *(.bss*) *(COMMON) } >RAM
}
