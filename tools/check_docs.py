#!/usr/bin/env python3
"""Check that README.md still describes the repository (run before committing).

  tools/check_docs.py            all checks, size tables rebuilt (tools/size_table.py --check)
  tools/check_docs.py --fast     skip the size tables (no builds)
  tools/check_docs.py --update   rewrite the size tables in README.md, then check the rest

Checks:
  - README size tables match a fresh build of every variant;
  - every board, family port, example, CMake preset and tool script is mentioned;
  - the number of bootloader configurations in "Testing" matches tools/build_matrix.sh.
It cannot judge prose: new options, transports or behaviour still need someone
to update the matching README section.
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fast", action="store_true", help="skip the size tables")
    ap.add_argument("--update", action="store_true", help="rewrite the size tables in README.md first")
    a = ap.parse_args()

    problems = []
    if not a.fast:
        r = subprocess.run([sys.executable, str(ROOT / "tools/size_table.py"), "--update" if a.update else "--check"],
                           capture_output=True, text=True)
        print(r.stdout.splitlines()[0] if r.stdout else r.stderr.strip())
        if r.returncode:
            problems.append("size tables in README.md are stale (tools/size_table.py --update)")

    readme = (ROOT / "README.md").read_text()

    def need(kind, names):
        for n in sorted(names):
            if n not in readme:
                problems.append(f"{kind} '{n}' is not mentioned in README.md")

    need("board", [p.name for p in (ROOT / "boards").iterdir() if (p / "umcub_config.h").is_file()])
    need("family port", [f"port/{p.name}" for p in (ROOT / "port").glob("stm32*") if p.is_dir()])
    need("example", [p.name for p in (ROOT / "examples").iterdir() if (p / "CMakeLists.txt").is_file()])
    presets = json.loads((ROOT / "CMakePresets.json").read_text())["configurePresets"]
    need("CMake preset", [p["name"] for p in presets if not p.get("hidden")])
    need("tool", [p.name for p in (ROOT / "tools").glob("*") if p.suffix in (".py", ".sh")] +
         [f"tools/hw/{p.name}" for p in (ROOT / "tools/hw").glob("*.py")])

    matrix = (ROOT / "tools/build_matrix.sh").read_text()
    block = matrix[matrix.index("configs=("):]
    block = block[:block.index("\n)")]
    n = len(re.findall(r'^\s*"[^"]+\|', block, re.M)) + matrix.count('dir="$OUT/custom-drivers"')
    m = re.search(r"# (\d+) bootloader configurations", readme)
    if not m or int(m.group(1)) != n:
        problems.append(f"README 'Testing' says {m.group(1) if m else '?'} bootloader configurations, "
                        f"tools/build_matrix.sh builds {n}")

    for p in problems:
        print("STALE:", p)
    print("README.md looks consistent" if not problems else f"{len(problems)} problem(s)")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
