#!/usr/bin/env bash
# Option bytes for umcub on STM32H745/H755 (both cores enabled).
#   tools/h755_option_bytes.sh single     CM4 boots into the bootloader vector table and
#                                         waits there (UMCUB_DUALCORE_SINGLE_BOOT)
#   tools/h755_option_bytes.sh per-core   CM4 boots its own bootloader in bank 2
#   tools/h755_option_bytes.sh factory    ST defaults (CM4 at 0x08100000) - same as per-core
#
# STM32_Programmer_CLI 2.23 maps H745/H755 to the single-core option-byte
# layout (no BOOT_CM4_ADD0), so the change is done by a small RAM helper run
# through GDB: tools/h7_ob/h7_ob.sh. FLASH_BOOT4 = ADD1[31:16] | ADD0[15:0]
# (address >> 16); ADD1 stays at the factory 0x1000.
set -euo pipefail
case "${1:-}" in
  single)            boot4=0x10000800 ;;
  per-core|factory)  boot4=0x10000810 ;;
  *) echo "usage: $0 single|per-core|factory" >&2; exit 2 ;;
esac
exec "$(dirname "$0")/h7_ob/h7_ob.sh" --boot4 "$boot4" --bcm7 1 --bcm4 1
