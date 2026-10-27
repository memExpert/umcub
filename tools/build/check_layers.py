#!/usr/bin/env python3
"""Check the layering of the umcub sources (.claude/skills/hw-abstraction-architecture).

  tools/build/check_layers.py        exit 1 and list every violation

Rules:
  - a file includes headers of its own layer or of lower layers only
    (L0 include/ config/ tests/configs/, L1 port/, L2 src/util/, L3 src/image/,
    L4 src/transport/, L5 src/proto/, L6 src/boot/, L7 boards/ lib/umcub_app/);
  - third-party headers from their lowest layer on (tinycrypt L2, MCUboot
    bootutil L3, tinyUSB L4, boot_serial / zcbor L5);
  - ST device / LL / CMSIS-core headers only in port/ and the board code boards/<b>/*.c;
  - no family checks (#if ... STM32...) outside port/ and boards/;
  - no local prototype / extern in a .c file of a symbol that is defined in a
    higher layer or not in the tree at all (generated data needs a header);
  - header names are unique across the layered directories (one flat include path).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAYERS = [
    ("include", 0), ("config", 0), ("tests/configs", 0),
    ("port", 1),
    ("src/util", 2),
    ("src/image", 3),
    ("src/transport", 4),
    ("src/proto", 5),
    ("src/boot", 6),
    ("boards", 7), ("lib/umcub_app", 7),
]
NAMES = {0: "L0 include/config", 1: "L1 port", 2: "L2 util", 3: "L3 image", 4: "L4 transport",
         5: "L5 proto", 6: "L6 boot", 7: "L7 board/app"}
# Third-party headers: lowest layer that may include them.
THIRD_PARTY = [
    (re.compile(r"^tinycrypt/"), 2),
    (re.compile(r"^(bootutil/|bootutil_priv\.h$|boot_serial/boot_serial_encryption\.h$)"), 3),
    (re.compile(r"^tusb\.h$"), 4),
    (re.compile(r"^(boot_serial/|boot_serial_priv\.h$|zcbor)"), 5),
]
SUFFIXES = {".c", ".h", ".inc", ".in"}
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
ST_HEADER = re.compile(r'^(stm32[a-z0-9]*xx|stm32[a-z0-9]*xx_ll_\w+|system_stm32\w*|core_cm\w+|cmsis_\w+)\.h$')
FAMILY_IF = re.compile(r'^\s*#\s*(if|elif|ifdef|ifndef)\b.*\bSTM32[A-Z][0-9A-Z]')
# A function or object definition at file scope / a declaration of one.
_TYPE = r"^(?!\s|#|typedef\b|static\b)"
# File-scope function definition / object definition (not extern) / declaration of either.
FUNC_DEF = re.compile(_TYPE + r"(?!extern\b)[\w\s\*\(\),]*?\b(umcub_\w+)\s*\([^;]*\)\s*\{?\s*$")
OBJ_DEF = re.compile(_TYPE + r"(?!extern\b)[\w\s\*\(\),]*?\b(umcub_\w+)\s*(\[[^\]]*\])?\s*[=;]")
DECLARATION = re.compile(_TYPE + r"(extern\b.*\b(umcub_\w+)\s*(?:\[[^\]]*\])?\s*;|[\w\s\*\(\),]*?\b(umcub_\w+)\s*\([^;]*\)\s*;)")


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
    text = {p: p.read_text(errors="replace").splitlines() for p in files}
    errors = []

    headers = {}
    for p in files:
        if p.suffix == ".h" and not rel[p].startswith("tests/configs/"):   # overlays: not on the include path
            headers.setdefault(p.name, []).append(p)
    for name, paths in headers.items():
        # one family port and one board are on the include path at a time
        if len(paths) > 1 and not all(re.match(r"(port/stm32|boards/)", rel[q]) for q in paths):
            errors.append(f"header name {name} is not unique: " + ", ".join(rel[q] for q in paths))

    # Lowest layer that defines each symbol (a weak default below a board override counts).
    defined = {}
    for p in files:
        if p.suffix == ".c":
            for line in text[p]:
                m = FUNC_DEF.match(line) or OBJ_DEF.match(line)
                if m:
                    lvl = layer_of(rel[p])
                    defined[m.group(1)] = min(lvl, defined.get(m.group(1), lvl))

    def resolve(src, inc):
        local = (src.parent / inc).resolve()
        if local in rel:
            return local
        cands = [p for p in headers.get(Path(inc).name, []) if rel[p].endswith("/" + inc) or p.name == inc]
        return cands[0] if cands else None

    for src in files:
        r = rel[src]
        lvl = layer_of(r)
        st_ok = r.startswith("port/") or re.fullmatch(r"boards/[^/]+/[^/]+\.c", r)
        for n, line in enumerate(text[src], 1):
            m = INCLUDE.match(line)
            if m:
                inc = m.group(1)
                if ST_HEADER.match(Path(inc).name) and not st_ok:
                    errors.append(f"{r}:{n}: device header {inc} outside port/ and board files")
                tgt = resolve(src, inc)
                if tgt is not None:
                    tl = layer_of(rel[tgt])
                    if tl > lvl:
                        errors.append(f"{r}:{n}: {NAMES[lvl]} includes {rel[tgt]} ({NAMES[tl]})")
                    if r.startswith("tests/configs/") and not rel[tgt].startswith("tests/configs/"):
                        errors.append(f"{r}:{n}: a config overlay includes {rel[tgt]}")
                else:
                    tl = next((tl for pat, tl in THIRD_PARTY if pat.search(inc)), 0)
                    if tl > lvl:
                        errors.append(f"{r}:{n}: {NAMES[lvl]} includes {inc} (allowed from {NAMES[tl]} on)")
                continue
            if FAMILY_IF.match(line) and not (r.startswith("port/") or r.startswith("boards/")):
                errors.append(f"{r}:{n}: family check outside port/: {line.strip()}")
            if src.suffix == ".c" and lvl < 7:
                d = DECLARATION.match(line)
                if d:
                    sym = d.group(2) or d.group(3)
                    if sym not in defined:
                        errors.append(f"{r}:{n}: local declaration of {sym}, defined outside the tree: "
                                      "declare it in a header")
                    elif defined[sym] > lvl:
                        errors.append(f"{r}:{n}: local declaration of {sym} ({NAMES[defined[sym]]}): "
                                      "declare the upcall in a header of this layer")

    for e in errors:
        print(e)
    print(f"check_layers: {len(files)} files, {len(errors)} violation(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
