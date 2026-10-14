#!/usr/bin/env python3
"""Check the layering of the umcub sources (.claude/skills/hw-abstraction-architecture).

  tools/build/check_layers.py        exit 1 and list every violation

Rules:
  - a file includes headers of its own layer or of lower layers only
    (L0 include/ config/, L1 port/, L2 util/, L3 image/, L4 transport/,
    L5 proto/, L6 boot/, L7 boards/ lib/umcub_app/);
  - ST device / LL headers only in port/stm32<fam>/ and boards/<b>/umcub_board.c;
  - no family checks (#if ... STM32...) outside port/ and boards/;
  - header names are unique across the layered directories (one flat include path).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAYERS = [
    ("include", 0), ("config", 0),
    ("port", 1),
    ("util", 2),
    ("image", 3),
    ("transport", 4),
    ("proto", 5),
    ("boot", 6),
    ("boards", 7), ("lib/umcub_app", 7),
]
NAMES = {0: "L0 include/config", 1: "L1 port", 2: "L2 util", 3: "L3 image", 4: "L4 transport",
         5: "L5 proto", 6: "L6 boot", 7: "L7 board/app"}
SUFFIXES = {".c", ".h", ".inc", ".in"}
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
ST_HEADER = re.compile(r'^(stm32[a-z0-9]*xx|stm32[a-z0-9]*xx_ll_\w+|system_stm32\w*)\.h$')
FAMILY_IF = re.compile(r'^\s*#\s*(if|elif|ifdef|ifndef)\b.*\bSTM32[A-Z][0-9A-Z]')


# Configuration data: assembled by config/umcub_cfg.h, so part of L0.
CONFIG_FILES = re.compile(r"^(boards/[^/]+/umcub_config\.h|port/stm32[^/]+/include/umcub_family_defaults\.h)$")


def layer_of(rel):
    if CONFIG_FILES.match(rel):
        return 0
    for prefix, level in LAYERS:
        if rel == prefix or rel.startswith(prefix + "/"):
            return level
    return None


def main():
    files = [p for prefix, _ in LAYERS for p in sorted((ROOT / prefix).rglob("*"))
             if p.is_file() and p.suffix in SUFFIXES]
    rel = {p: p.relative_to(ROOT).as_posix() for p in files}
    errors = []

    headers = {}
    for p in files:
        if p.suffix == ".h":
            headers.setdefault(p.name, []).append(p)
    for name, paths in headers.items():
        # one family port and one board are on the include path at a time
        if len(paths) > 1 and not all(re.match(r"(port/stm32|boards/)", rel[q]) for q in paths):
            errors.append(f"header name {name} is not unique: " + ", ".join(rel[q] for q in paths))

    def resolve(src, inc):
        local = (src.parent / inc).resolve()
        if local in rel:
            return local
        cands = [p for p in headers.get(Path(inc).name, []) if rel[p].endswith("/" + inc) or p.name == inc]
        return cands[0] if cands else None

    for src in files:
        r = rel[src]
        lvl = layer_of(r)
        st_ok = r.startswith("port/stm32") or re.fullmatch(r"boards/[^/]+/umcub_board\.c", r)
        for n, line in enumerate(src.read_text(errors="replace").splitlines(), 1):
            m = INCLUDE.match(line)
            if m:
                inc = m.group(1)
                if ST_HEADER.match(Path(inc).name) and not st_ok:
                    errors.append(f"{r}:{n}: ST header {inc} outside port/stm32<fam>/ and board files")
                tgt = resolve(src, inc)
                if tgt is not None:
                    tl = layer_of(rel[tgt])
                    if tl > lvl:
                        errors.append(f"{r}:{n}: {NAMES[lvl]} includes {rel[tgt]} ({NAMES[tl]})")
            elif FAMILY_IF.match(line) and not (r.startswith("port/") or r.startswith("boards/")):
                errors.append(f"{r}:{n}: family check outside port/: {line.strip()}")

    for e in errors:
        print(e)
    print(f"check_layers: {len(files)} files, {len(errors)} violation(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
