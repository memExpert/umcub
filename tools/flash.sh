#!/usr/bin/env bash
# Program bootloader and signed images over SWD (ST-LINK).
#   tools/flash.sh <bootloader.hex> [image.signed.hex ...]
# Signed .hex files already carry their slot address.
set -euo pipefail
CLI=${STM32_PROGRAMMER_CLI:-STM32_Programmer_CLI}
[[ $# -ge 1 ]] || { echo "usage: $0 boot.hex [app.signed.hex ...]" >&2; exit 2; }
for f in "$@"; do
  "$CLI" -c port=SWD mode=UR -d "$f" -v
done
"$CLI" -c port=SWD -rst
