#!/usr/bin/env bash
# Program STM32H7 dual-core option bytes through a RAM helper + GDB.
#   tools/h7_ob/h7_ob.sh [--boot7 0xADD1ADD0] [--boot4 0xADD1ADD0] [--bcm7 0|1 --bcm4 0|1]
# Example (CM4 boots from the CM7 vector table, ADD1 kept at 0x1000):
#   tools/h7_ob/h7_ob.sh --boot4 0x10000800
# BOOTx values are FLASH_BOOTx registers: ADD1[31:16] | ADD0[15:0], address >> 16.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
BUNDLES=${STM32CUBE_BUNDLES:-$HOME/.local/share/stm32cube/bundles}
GDBSRV=${GDBSRV:-$(ls -d "$BUNDLES"/stlink-gdbserver/*/bin | head -1)/ST-LINK_gdbserver}
CPDIR=${CPDIR:-$(ls -d "$BUNDLES"/programmer/*/bin | head -1)}
GDB=${GDB:-$(ls "$BUNDLES"/gnu-gdb-for-stm32/*/bin/arm-none-eabi-gdb | head -1)}
PORT=${PORT:-61238}

boot7=0xFFFFFFFF boot4=0xFFFFFFFF bcm7= bcm4=
while [[ $# -gt 0 ]]; do
  case "$1" in
    --boot7) boot7=$2; shift 2 ;;
    --boot4) boot4=$2; shift 2 ;;
    --bcm7) bcm7=$2; shift 2 ;;
    --bcm4) bcm4=$2; shift 2 ;;
    *) echo "unknown argument $1" >&2; exit 2 ;;
  esac
done
bcm=0xFFFFFFFF
if [[ -n "$bcm7$bcm4" ]]; then
  [[ -n "$bcm7" && -n "$bcm4" ]] || { echo "--bcm7 and --bcm4 go together" >&2; exit 2; }
  bcm=$(( bcm7 | (bcm4 << 1) ))
fi

OUT=${TMPDIR:-/tmp}/h7_ob.$$
mkdir -p "$OUT"
trap 'kill $SRV 2>/dev/null || true; rm -rf "$OUT"' EXIT
arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -Os -g -nostdlib -ffreestanding -Wall -Wextra \
  -Wl,--no-warn-rwx-segments -T "$HERE/ob_write.ld" "$HERE/ob_write.c" -o "$OUT/ob_write.elf"

"$GDBSRV" -d -g -p "$PORT" -cp "$CPDIR" >"$OUT/gdbserver.log" 2>&1 &
SRV=$!
sleep 3

cat >"$OUT/run.gdb" <<GDB
set pagination off
set confirm off
target remote localhost:$PORT
monitor halt
load
set var ob_request.magic = 0x0B0B0B0B
set var ob_request.boot7 = $boot7
set var ob_request.boot4 = $boot4
set var ob_request.bcm = $bcm
set var ob_result.status = 0
set \$primask = 1
set \$sp = 0x24004000
set \$pc = ob_entry
continue
printf "status    %08x\n", ob_result.status
printf "OPTSR_CUR %08x  (BCM7 %d, BCM4 %d)\n", ob_result.optsr_cur, (ob_result.optsr_cur >> 23) & 1, (ob_result.optsr_cur >> 22) & 1
printf "BOOT7_CUR %08x\n", ob_result.boot7_cur
printf "BOOT4_CUR %08x\n", ob_result.boot4_cur
monitor reset
detach
GDB
timeout 120 "$GDB" -batch -x "$OUT/run.gdb" "$OUT/ob_write.elf" 2>&1 | grep -E "status|OPTSR|BOOT[47]_CUR|rror"
