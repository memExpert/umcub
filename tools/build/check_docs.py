#!/usr/bin/env python3
"""Check that README.md and docs/ still describe the repository (run before committing).

  tools/build/check_docs.py            all checks, size tables rebuilt (tools/build/size_table.py --check)
  tools/build/check_docs.py --fast     skip the size tables (no builds)
  tools/build/check_docs.py --update   rewrite the size tables in docs/size.md, then check the rest

Checks:
  - the size tables in docs/size.md match a fresh build of every variant;
  - every board, family port, example, CMake preset and tool script is mentioned (README or docs/);
  - the number of bootloader configurations in docs/testing.md matches tools/build/build_matrix.sh;
  - every relative link points to a file that exists and, with #anchor, to a heading in it.
It cannot judge prose: new options, transports or behaviour still need someone
to update the matching section.
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DOCS = [ROOT / "README.md"] + sorted((ROOT / "docs").glob("*.md"))


def anchors(text):
    """GitHub heading anchors of a Markdown text (outside code blocks)."""
    out, fence = set(), False
    for line in text.splitlines():
        if line.startswith("```"):
            fence = not fence
        m = None if fence else re.match(r"#{1,6} (.*)", line)
        if m:
            out.add(re.sub(r"[^\w\- ]", "", m.group(1).strip().lower()).replace(" ", "-"))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fast", action="store_true", help="skip the size tables")
    ap.add_argument("--update", action="store_true", help="rewrite the size tables first")
    a = ap.parse_args()

    problems = []
    if not a.fast:
        r = subprocess.run([sys.executable, str(ROOT / "tools/build/size_table.py"), "--update" if a.update else "--check"],
                           capture_output=True, text=True)
        print(r.stdout.splitlines()[0] if r.stdout else r.stderr.strip())
        if r.returncode:
            problems.append("size tables in docs/size.md are stale (tools/build/size_table.py --update)")

    texts = {p: p.read_text() for p in DOCS}
    all_text = "\n".join(texts.values())

    def need(kind, names):
        for n in sorted(names):
            if n not in all_text:
                problems.append(f"{kind} '{n}' is not mentioned in README.md or docs/")

    need("board", [p.name for p in (ROOT / "boards").iterdir() if (p / "umcub_config.h").is_file()])
    need("family port", [f"port/{p.name}" for p in (ROOT / "port").glob("stm32*") if p.is_dir()])
    need("example", [p.name for p in (ROOT / "examples").iterdir() if (p / "CMakeLists.txt").is_file()])
    presets = json.loads((ROOT / "CMakePresets.json").read_text())["configurePresets"]
    need("CMake preset", [p["name"] for p in presets if not p.get("hidden")])
    need("tool", [p.name for p in (ROOT / "tools").rglob("*")
                  if p.suffix in (".py", ".sh") and "__pycache__" not in p.parts])
    need("source directory", [f"{d}/" for d in ("include", "config", "port", "src", "lib/umcub_app", "tests/configs")]
         + [f"  {d}/" for d in ("util", "image", "transport", "proto", "boot")])   # under src/ in the layout

    matrix = (ROOT / "tools/build/build_matrix.sh").read_text()
    block = matrix[matrix.index("configs=("):]
    block = block[:block.index("\n)")]
    n = len(re.findall(r'^\s*"[^"]+\|', block, re.M)) + matrix.count('dir="$OUT/custom-drivers"')
    m = re.search(r"# (\d+) bootloader configurations", texts[ROOT / "docs/testing.md"])
    if not m or int(m.group(1)) != n:
        problems.append(f"docs/testing.md says {m.group(1) if m else '?'} bootloader configurations, "
                        f"tools/build/build_matrix.sh builds {n}")

    for p, text in texts.items():
        for target in re.findall(r"\]\(([^)\s]+)\)", text):
            if re.match(r"[a-z]+:", target):
                continue                                    # https:, mailto:
            path, _, anchor = target.partition("#")
            dest = (p.parent / path).resolve() if path else p
            if not dest.exists():
                problems.append(f"{p.relative_to(ROOT)}: link to {target}: no such file")
            elif anchor and dest.suffix == ".md" and anchor not in anchors(dest.read_text()):
                problems.append(f"{p.relative_to(ROOT)}: link to {target}: no such heading")

    for p in problems:
        print("STALE:", p)
    print("README.md and docs/ look consistent" if not problems else f"{len(problems)} problem(s)")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
