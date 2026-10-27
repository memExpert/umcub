#!/usr/bin/env bash
# The build matrix (tests/CMakeLists.txt, configurations in tests/matrix/configs.txt):
# every bootloader configuration, presets, examples, checks, host tests, link e2e;
# fails on warnings. Runs it with CTest and prints the size summary.
#   tools/build/build_matrix.sh [build-dir] [ctest options, e.g. -R bluepill]   (JOBS=n: tests at once)
set -uo pipefail
cd "$(dirname "$0")/../.."
OUT=${1:-build/matrix}
shift $(( $# > 0 ? 1 : 0 ))
JOBS=${JOBS:-$(( $(nproc) / 4 > 1 ? $(nproc) / 4 : 1 ))}     # each build is parallel inside as well

cmake -S tests -B "$OUT" -G Ninja >"$OUT.configure.log" 2>&1 || { cat "$OUT.configure.log"; exit 1; }
rm -rf "$OUT/summary"
ctest --test-dir "$OUT" -j "$JOBS" --output-on-failure "$@" >"$OUT.ctest.log" 2>&1
fail=$?
grep -E "^ *[0-9]+/[0-9]+ Test .*(Failed|Not Run|Timeout)|^FAIL" "$OUT.ctest.log"
while read -r t; do
  f="$OUT/summary/$t"
  [[ -f "$f" ]] && cat "$f"
done <"$OUT/summary_order.txt"
tail -n +$(grep -n "tests passed" "$OUT.ctest.log" | cut -d: -f1 | tail -1) "$OUT.ctest.log" 2>/dev/null | head -1
exit $fail
