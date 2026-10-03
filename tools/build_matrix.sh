#!/usr/bin/env bash
# Builds every supported configuration of the bootloader for the reference
# boards plus the examples; fails on warnings or if a bootloader does not fit.
#   tools/build_matrix.sh [build-dir]
set -uo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-build/matrix}
BOARD=${BOARD:-nucleo_h755zi_q}
mkdir -p "$OUT"
fail=0
summary=()

# name | core | build type | pre overlay | post overlay | board (default $BOARD)
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
  "usb-dfu-only|cm7|Release||usb_dfu_only.h"
  "single-core|cm7|Release|single_core.h|"
  "per-core-cm7|cm7|Release|per_core.h|"
  "per-core-cm4|cm4|Release|per_core.h|"
  "all-debug|cm7|Debug||"
  "bluepill-f103|cm3|Release|||bluepill_f103c8"
  "bluepill-f103-usb|cm3|Release||bluepill_usb.h|bluepill_f103c8"
  "rs485-hw-de|cm7|Release||rs485.h"
  "bluepill-rs485|cm3|Release||bluepill_rs485.h|bluepill_f103c8"
  "link-addressed|cm7|Release||link_addressed.h"
  "bluepill-rs485-link|cm3|Release||bluepill_rs485_link.h|bluepill_f103c8"
  "link-secure|cm7|Release||link_secure.h"
  "bluepill-rs485-secure|cm3|Release||bluepill_rs485_secure.h|bluepill_f103c8"
  "encrypt-images|cm7|Release||encrypt_images.h"
  "bluepill-encrypt|cm3|Release||bluepill_encrypt.h|bluepill_f103c8"
  "per-core-cm4-encrypt|cm4|Release|per_core.h|encrypt_only.h"
)

for c in "${configs[@]}"; do
  IFS='|' read -r name core type pre post board <<<"$c"
  dir="$OUT/$name"
  args=(-G Ninja -DCMAKE_BUILD_TYPE="$type" -DUMCUB_BOARD="${board:-$BOARD}" -DUMCUB_CORE="$core")
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

# Board-supplied CAN/ETH drivers and board transport (tests/boards/custom_drivers):
# the family FDCAN/ETH drivers must be left out and the board's linked instead.
dir="$OUT/custom-drivers"
if cmake -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DUMCUB_BOARD="$PWD/tests/boards/custom_drivers" \
     -DUMCUB_CORE=cm7 >"$dir.configure.log" 2>&1 && cmake --build "$dir" >"$dir.build.log" 2>&1 &&
   ! grep -q "warning:" "$dir.build.log" && ! grep -qE "fdcan\.c|/eth\.c" "$dir/build.ninja"; then
  summary+=("$(printf '%-24s %s' "custom-drivers" "$(grep -E "^ +FLASH:" "$dir.build.log" | awk '{print $2, $3, $6}')")")
else
  echo "FAIL custom-drivers (see $dir.*.log)"; fail=1
fi

# CMake presets (bootloader + the board's example applications in one tree,
# UMCUB_BUILD_EXAMPLES): every target links, warning-free, .map next to each .elf.
for p in h755-cm7 h755-per-core-cm4 bluepill-usb; do
  dir="$OUT/preset-$p"
  if cmake --preset "$p" -B "$dir" >"$dir.configure.log" 2>&1 && cmake --build "$dir" >"$dir.build.log" 2>&1 &&
     ! grep -q "warning:" "$dir.build.log"; then
    elfs=$(cd "$dir" && ls *.elf examples/*/*.elf 2>/dev/null)
    missing=$(for e in $elfs; do [[ -f "$dir/${e%.elf}.map" ]] || echo "$e"; done)
    if [[ -z "$missing" ]]; then
      summary+=("$(printf '%-24s %s' "preset $p" "$(echo $elfs | wc -w) targets: $(echo $elfs | xargs -n1 basename | tr '\n' ' ')")")
    else
      echo "FAIL preset $p: no .map for $missing"; fail=1
    fi
  else
    echo "FAIL preset $p (see $dir.*.log)"; fail=1
  fi
done

# example dir | board | core | image
examples=(
  "h755_cm7_app|nucleo_h755zi_q|cm7|0"
  "h755_cm4_app|nucleo_h755zi_q|cm4|1"
  "bluepill_app|bluepill_f103c8||0"
)
for e in "${examples[@]}"; do
  IFS='|' read -r name eboard ecore eimage <<<"$e"
  dir="$OUT/$name"
  if cmake -S "examples/$name" -B "$dir" -G Ninja >"$dir.configure.log" 2>&1 && cmake --build "$dir" >"$dir.build.log" 2>&1 &&
     ! grep -q "warning:" "$dir.build.log" &&
     .venv/bin/imgtool verify -k tools/keys/dev-ecdsa-p256.pem "$dir/$name.signed.bin" >"$dir.verify.log" 2>&1 &&
     # IDE path (tools/umcub_image.py) must produce the same image as CMake.
     .venv/bin/python tools/umcub_image.py sign --board "$eboard" ${ecore:+--core "$ecore"} --image "$eimage" \
       --version "$(grep -m1 -oP 'Image version: \K[0-9.]+(?=\+)' "$dir.verify.log")" "$dir/$name.elf" -o "$dir/ide" >>"$dir.verify.log" 2>&1 &&
     .venv/bin/python tests/tools/compare_images.py "$dir/$name.signed.bin" "$dir/ide.signed.bin" >>"$dir.verify.log" 2>&1; then
    summary+=("$(printf '%-24s signed, %s, IDE signing identical' "$name" "$(grep -m1 'Image version' "$dir.verify.log")")")
  else
    echo "FAIL example $name (see $dir.*.log)"; fail=1
  fi
done

# IDE integration (README "Using umcub from an IDE"): umcub_app_all.c with only
# the documented include paths and the CMSIS CORE_CMx define, also with clang
# (armclang in Keil MDK is clang based).
ide_incs=(lib/umcub_app/include config "boards/$BOARD" port/stm32h7/include port/include mcuboot_port/include
          third_party/mcuboot/boot/bootutil/include third_party/st/cmsis_device_h7/Include
          third_party/st/stm32h7xx_hal_driver/Inc third_party/cmsis_core/Include)
ide_flags=(-Os -Wall -Wextra -Werror -Wno-unused-parameter -DSTM32H755xx -DCORE_CM7 "${ide_incs[@]/#/-I}" -c lib/umcub_app/umcub_app_all.c)
ide_ok="gcc"
arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard "${ide_flags[@]}" -o "$OUT/ide_gcc.o" \
  >"$OUT/ide.log" 2>&1 || { echo "FAIL IDE build (gcc), see $OUT/ide.log"; fail=1; ide_ok=""; }
if command -v clang >/dev/null; then
  # newlib headers: <sysroot>/include; distribution toolchains (Debian/Ubuntu)
  # print no sysroot, there they sit next to libc.a's lib directory.
  newlib_inc="$(arm-none-eabi-gcc -print-sysroot)/include"
  [[ -f "$newlib_inc/string.h" ]] || newlib_inc="$(dirname "$(arm-none-eabi-gcc -print-file-name=libc.a)")/../include"
  clang --target=arm-none-eabi -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -Wno-unknown-attributes \
    -isystem "$newlib_inc" "${ide_flags[@]}" -o "$OUT/ide_clang.o" >>"$OUT/ide.log" 2>&1 &&
    ide_ok+=" clang" || { echo "FAIL IDE build (clang), see $OUT/ide.log"; fail=1; }
fi
summary+=("$(printf '%-24s umcub_app_all.c: %s' "ide-library" "$ide_ok")")

# Host tests: real MCUboot + umcub transports on emulated flash.
if cmake -S tests/host -B "$OUT/host" -G Ninja >"$OUT/host.configure.log" 2>&1 &&
   cmake --build "$OUT/host" >"$OUT/host.build.log" 2>&1 &&
   ctest --test-dir "$OUT/host" --output-on-failure >"$OUT/host.test.log" 2>&1; then
  summary+=("$(printf '%-24s %s' "host-tests" "$(grep -m1 'tests passed' "$OUT/host.test.log")")")
else
  echo "FAIL host tests (see $OUT/host.*.log)"; fail=1
fi

# Several simulated devices on one bus with tools/umcub_link.py and smpmgr.
if .venv/bin/python tests/host/link_e2e.py "$OUT/host" --python .venv/bin/python >"$OUT/link_e2e.log" 2>&1; then
  summary+=("$(printf '%-24s %s' "link-e2e" "$(tail -1 "$OUT/link_e2e.log")")")
else
  echo "FAIL link e2e (see $OUT/link_e2e.log)"; fail=1
fi

printf '%s\n' "${summary[@]}"
exit $fail
