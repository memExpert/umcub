#!/usr/bin/env bash
# Builds every supported configuration of the bootloader for the reference
# boards plus the examples; fails on warnings or if a bootloader does not fit.
#   tools/build/build_matrix.sh [build-dir]      (JOBS=n: configurations built at once)
set -uo pipefail
cd "$(dirname "$0")/../.."
OUT=${1:-build/matrix}
BOARD=${BOARD:-nucleo_h755zi_q}
mkdir -p "$OUT"
fail=0
summary=()

# Layering (include/ ... boot/, see the hw-abstraction-architecture skill).
if python3 tools/build/check_layers.py >"$OUT/layers.log" 2>&1; then
  summary+=("$(printf '%-24s %s' "layers" "$(tail -1 "$OUT/layers.log")")")
else
  echo "FAIL layering (see $OUT/layers.log)"; cat "$OUT/layers.log"; fail=1
fi

# name | core | build type | pre overlay | post overlay | board (default $BOARD)
configs=(
  "all-swap-scratch|cm7|Release||"
  "all-overwrite|cm7|Release|mode/overwrite.h|"
  "all-swap-move|cm7|Release|mode/swap_move.h|"
  "all-swap-offset|cm7|Release|mode/swap_offset.h|"
  "all-direct-xip|cm7|Release|mode/direct_xip.h|"
  "all-direct-xip-revert|cm7|Release|mode/direct_xip_revert.h|"
  "uart-only|cm7|Release||transport/uart_only.h"
  "uart-tx-ring|cm7|Release||feature/uart_tx_ring.h"
  "uart-usb|cm7|Release||transport/uart_usb.h"
  "uart-canfd-loopback|cm7|Release||transport/uart_can.h"
  "usb-dfu-only|cm7|Release||transport/usb_dfu_only.h"
  "single-core|cm7|Release|feature/single_core.h|"
  "per-core-cm7|cm7|Release|feature/per_core.h|"
  "per-core-cm4|cm4|Release|feature/per_core.h|"
  "all-debug|cm7|Debug||"
  "bluepill-f103|cm3|Release|||bluepill_f103c8"
  "bluepill-f103-usb|cm3|Release||bluepill/usb.h|bluepill_f103c8"
  "rs485-hw-de|cm7|Release||transport/rs485.h"
  "bluepill-rs485|cm3|Release||bluepill/rs485.h|bluepill_f103c8"
  "link-addressed|cm7|Release||feature/link_addressed.h"
  "bluepill-rs485-link|cm3|Release||bluepill/rs485_link.h|bluepill_f103c8"
  "bluepill-node-addr-flash|cm3|Release||bluepill/node_addr_flash.h|bluepill_f103c8"
  "link-secure|cm7|Release||feature/link_secure.h"
  "bluepill-rs485-secure|cm3|Release||bluepill/rs485_secure.h|bluepill_f103c8"
  "lite-upload|cm7|Release||feature/lite_upload.h"
  "minimal-template|cm7|Release|||$PWD/tests/boards/minimal"     # config/umcub_config_minimal.h as it ships
  "bluepill-lite|cm3|Release||feature/lite_upload.h|bluepill_f103c8"
  "bluepill-sign-none|cm3|Release||feature/sign_none.h|bluepill_f103c8"
  "bluepill-tx-ring|cm3|Release||feature/uart_tx_ring.h|bluepill_f103c8"
  "lto|cm7|Release||feature/lto.h"
  "bluepill-lto|cm3|Release||feature/lto.h|bluepill_f103c8"
  "bluepill-can|cm3|Release||bluepill/can.h|bluepill_f103c8"
  "encrypt-images|cm7|Release||feature/encrypt_images.h"
  "bluepill-encrypt|cm3|Release||bluepill/encrypt.h|bluepill_f103c8"
  "per-core-cm4-encrypt|cm4|Release|feature/per_core.h|feature/encrypt_only.h"
  "blackpill-f411|cm4|Release|||blackpill_f411ce"
  "blackpill-f411-rs485|cm4|Release||blackpill/rs485.h|blackpill_f411ce"
  "blackpill-f411-usart2|cm4|Release||blackpill/usart2.h|blackpill_f411ce"
  "blackpill-f411-rs485-link|cm4|Release||blackpill/rs485_link.h|blackpill_f411ce"
  "blackpill-f411-encrypt|cm4|Release||blackpill/encrypt.h|blackpill_f411ce"
  "blackpill-f411-usb|cm4|Release||blackpill/usb.h|blackpill_f411ce"
)

# One configuration: configure, build, warnings. Writes $dir.result: OK or
# FAIL, the messages to print, the summary line (prefix SUMMARY).
build_config() {
  local name core type pre post board
  IFS='|' read -r name core type pre post board <<<"$1"
  local dir="$OUT/$name"
  local args=(-G Ninja -DCMAKE_BUILD_TYPE="$type" -DUMCUB_BOARD="${board:-$BOARD}" -DUMCUB_CORE="$core")
  [[ -n "$pre" ]] && args+=(-DUMCUB_CONFIG_PRE="tests/configs/$pre")
  [[ -n "$post" ]] && args+=(-DUMCUB_CONFIG_POST="tests/configs/$post")
  {
    if ! cmake -B "$dir" "${args[@]}" >"$dir.configure.log" 2>&1; then
      echo FAIL; echo "FAIL configure $name (see $dir.configure.log)"; return
    fi
    if ! cmake --build "$dir" >"$dir.build.log" 2>&1; then
      echo FAIL; echo "FAIL build $name (see $dir.build.log)"; return
    fi
    if grep -q "warning:" "$dir.build.log"; then
      echo FAIL; echo "FAIL warnings in $name:"; grep "warning:" "$dir.build.log" | head -5
    else
      echo OK
    fi
    printf 'SUMMARY %-24s %s\n' "$name" "$(grep -E "^ +FLASH:" "$dir.build.log" | awk '{print $2, $3, $6}')"
  } >"$dir.result"
}

# Configurations side by side (each build is parallel inside as well);
# results in the order of the list.
JOBS=${JOBS:-$(( $(nproc) / 4 > 1 ? $(nproc) / 4 : 1 ))}
for c in "${configs[@]}"; do
  while (( $(jobs -rp | wc -l) >= JOBS )); do wait -n; done
  build_config "$c" &
done
wait
for c in "${configs[@]}"; do
  r="$OUT/${c%%|*}.result"
  [[ "$(head -1 "$r" 2>/dev/null)" == OK ]] || fail=1
  grep -v '^OK$\|^FAIL$\|^SUMMARY ' "$r" 2>/dev/null || true
  line=$(sed -n 's/^SUMMARY //p' "$r" 2>/dev/null)
  [[ -n "$line" ]] && summary+=("$line")
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
  "blackpill_app|blackpill_f411ce||0"
)
for e in "${examples[@]}"; do
  IFS='|' read -r name eboard ecore eimage <<<"$e"
  dir="$OUT/$name"
  if cmake -S "examples/$name" -B "$dir" -G Ninja >"$dir.configure.log" 2>&1 && cmake --build "$dir" >"$dir.build.log" 2>&1 &&
     ! grep -q "warning:" "$dir.build.log" &&
     .venv/bin/imgtool verify -k tools/keys/dev-ecdsa-p256.pem "$dir/$name.signed.bin" >"$dir.verify.log" 2>&1 &&
     # CMake signs the ELF with tools/host/umcub_image.py; its ELF loader must
     # see what objcopy sees: signing the objcopy binary gives the same image.
     arm-none-eabi-objcopy -O binary "$dir/$name.elf" "$dir/objcopy.bin" &&
     .venv/bin/python tools/host/umcub_image.py sign --board "$eboard" ${ecore:+--core "$ecore"} --image "$eimage" \
       --version "$(grep -m1 -oP 'Image version: \K[0-9.]+(?=\+)' "$dir.verify.log")" "$dir/objcopy.bin" -o "$dir/objcopy" >>"$dir.verify.log" 2>&1 &&
     .venv/bin/python tests/tools/compare_images.py "$dir/$name.signed.bin" "$dir/objcopy.signed.bin" >>"$dir.verify.log" 2>&1; then
    summary+=("$(printf '%-24s signed, %s, ELF = objcopy' "$name" "$(grep -m1 'Image version' "$dir.verify.log")")")
  else
    echo "FAIL example $name (see $dir.*.log)"; fail=1
  fi
done

# IDE integration (docs/application.md, "Using umcub from an IDE"): umcub_app_all.c with only
# the documented include paths and the CMSIS CORE_CMx define, also with clang
# (armclang in Keil MDK is clang based).
ide_incs=(include config "boards/$BOARD" port/stm32h7/include port/include src/image/mcuboot/include
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
# The single translation unit in every upgrade mode (mode-specific code must
# not clash: static names are shared by all library sources there).
for m in overwrite swap_move swap_offset direct_xip direct_xip_revert; do
  arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard \
    "-DUMCUB_CONFIG_PRE=\"$PWD/tests/configs/mode/$m.h\"" "${ide_flags[@]}" -o "$OUT/ide_$m.o" >>"$OUT/ide.log" 2>&1 ||
    { echo "FAIL IDE build ($m), see $OUT/ide.log"; fail=1; ide_ok+=" ($m FAILED)"; }
done
summary+=("$(printf '%-24s umcub_app_all.c: %s, all upgrade modes' "ide-library" "$ide_ok")")

# Pin check (tools/build/check_pins.py): right and wrong pins, packages.
if python3 tests/tools/check_pins_test.py >"$OUT/check_pins.log" 2>&1; then
  summary+=("$(printf '%-24s %s' "pin-check" "$(tail -1 "$OUT/check_pins.log")")")
else
  echo "FAIL pin check (see $OUT/check_pins.log)"; cat "$OUT/check_pins.log"; fail=1
fi

# Host tests: real MCUboot + umcub transports on emulated flash.
if cmake -S tests/host -B "$OUT/host" -G Ninja >"$OUT/host.configure.log" 2>&1 &&
   cmake --build "$OUT/host" >"$OUT/host.build.log" 2>&1 &&
   ctest --test-dir "$OUT/host" --output-on-failure >"$OUT/host.test.log" 2>&1; then
  summary+=("$(printf '%-24s %s' "host-tests" "$(grep -m1 'tests passed' "$OUT/host.test.log")")")
else
  echo "FAIL host tests (see $OUT/host.*.log)"; fail=1
fi

# Several simulated devices on one bus with tools/host/umcub_link.py and smpmgr.
if .venv/bin/python tests/host/link_e2e.py "$OUT/host" --python .venv/bin/python >"$OUT/link_e2e.log" 2>&1; then
  summary+=("$(printf '%-24s %s' "link-e2e" "$(tail -1 "$OUT/link_e2e.log")")")
else
  echo "FAIL link e2e (see $OUT/link_e2e.log)"; fail=1
fi

printf '%s\n' "${summary[@]}"
exit $fail
