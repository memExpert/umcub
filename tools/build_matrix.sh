#!/usr/bin/env bash
# Builds every supported configuration of the bootloader for the reference
# board plus the examples; fails on warnings or if a bootloader does not fit.
#   tools/build_matrix.sh [build-dir]
set -uo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-build/matrix}
BOARD=${BOARD:-nucleo_h755zi_q}
mkdir -p "$OUT"
fail=0
summary=()

# name | core | build type | pre overlay | post overlay
configs=(
  "all-swap-scratch|cm7|Release||"
  "all-overwrite|cm7|Release|mode_overwrite.h|"
  "all-swap-move|cm7|Release|mode_swap_move.h|"
  "all-swap-offset|cm7|Release|mode_swap_offset.h|"
  "all-direct-xip|cm7|Release|mode_direct_xip.h|"
  "all-direct-xip-revert|cm7|Release|mode_direct_xip_revert.h|"
  "uart-only|cm7|Release||uart_only.h"
  "uart-usb|cm7|Release||uart_usb.h"
  "uart-canfd-loopback|cm7|Release||uart_can.h"
  "single-core|cm7|Release|single_core.h|"
  "per-core-cm7|cm7|Release|per_core.h|"
  "per-core-cm4|cm4|Release|per_core.h|"
  "all-debug|cm7|Debug||"
)

for c in "${configs[@]}"; do
  IFS='|' read -r name core type pre post <<<"$c"
  dir="$OUT/$name"
  args=(-G Ninja -DCMAKE_BUILD_TYPE="$type" -DUMCUB_BOARD="$BOARD" -DUMCUB_CORE="$core")
  [[ -n "$pre" ]] && args+=(-DUMCUB_CONFIG_PRE="tools/config/$pre")
  [[ -n "$post" ]] && args+=(-DUMCUB_CONFIG_POST="tools/config/$post")
  if ! cmake -B "$dir" "${args[@]}" >"$dir.configure.log" 2>&1; then
    echo "FAIL configure $name (see $dir.configure.log)"; fail=1; continue
  fi
  if ! cmake --build "$dir" >"$dir.build.log" 2>&1; then
    echo "FAIL build $name (see $dir.build.log)"; fail=1; continue
  fi
  if grep -q "warning:" "$dir.build.log"; then
    echo "FAIL warnings in $name:"; grep "warning:" "$dir.build.log" | head -5; fail=1
  fi
  used=$(grep -E "^ +FLASH:" "$dir.build.log" | awk '{print $2, $3, $6}')
  summary+=("$(printf '%-24s %s' "$name" "$used")")
done

for ex in examples/h755_cm7_app examples/h755_cm4_app; do
  name=$(basename "$ex")
  dir="$OUT/$name"
  if cmake -S "$ex" -B "$dir" -G Ninja >"$dir.configure.log" 2>&1 && cmake --build "$dir" >"$dir.build.log" 2>&1 &&
     .venv/bin/imgtool verify -k tools/keys/dev-ecdsa-p256.pem "$dir/$name.signed.bin" >"$dir.verify.log" 2>&1; then
    summary+=("$(printf '%-24s signed, %s' "$name" "$(grep -m1 'Image version' "$dir.verify.log")")")
  else
    echo "FAIL example $name (see $dir.*.log)"; fail=1
  fi
done

# Host tests: real MCUboot + umcub transports on emulated flash.
if cmake -S tests/host -B "$OUT/host" -G Ninja >"$OUT/host.configure.log" 2>&1 &&
   cmake --build "$OUT/host" >"$OUT/host.build.log" 2>&1 &&
   ctest --test-dir "$OUT/host" --output-on-failure >"$OUT/host.test.log" 2>&1; then
  summary+=("$(printf '%-24s %s' "host-tests" "$(grep -m1 'tests passed' "$OUT/host.test.log")")")
else
  echo "FAIL host tests (see $OUT/host.*.log)"; fail=1
fi

printf '%s\n' "${summary[@]}"
exit $fail
