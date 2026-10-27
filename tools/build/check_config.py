#!/usr/bin/env python3
"""The configuration as CMake read it (cmake/umcub_config.cmake writes
<build>/umcub_probe/config.txt, cmake/umcub_app.cmake app_windows.txt) against
what the host tools read on their own (tools/host/umcub/config.py): every
numeric UMCUB_CFG_* value and every application slot window must agree.

  check_config.py <build dir>...
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/host"))
from umcub.config import Config  # noqa: E402
from umcub.errors import UmcubError  # noqa: E402


def read(path):
    meta, values = {}, {}
    for line in path.read_text().splitlines():
        k, _, v = line.partition("=")
        (meta if k.startswith("@") else values)[k.lstrip("@")] = v
    return meta, values


def check(build):
    probe = Path(build) / "umcub_probe"
    meta, values = read(probe / "config.txt")
    core = meta["core"] if meta["core"] in ("cm7", "cm4") else None
    try:
        cfg = Config(meta["board"], core, meta["pre"] or None, meta["post"] or None, meta["cc"])
    except UmcubError as e:
        return [f"umcub.config: {e}"]
    bad = []
    for name, v in values.items():
        mine = cfg.v.get(name.removeprefix("UMCUB_CFG_"))
        if mine != int(v):
            bad.append(f"{name}: CMake {v}, umcub.config {mine}")
    windows = probe / "app_windows.txt"
    if windows.is_file():
        for k, v in read(windows)[0].items():
            img, slot = map(int, k.removeprefix("window_").split("_"))
            want = tuple(int(x, 16) for x in v.split(","))
            if cfg.window(img, slot) != want:
                bad.append(f"window image {img} slot {slot}: CMake {v}, umcub.config "
                           f"{','.join(hex(x) for x in cfg.window(img, slot))}")
    return bad


def main():
    dirs = [d for d in sys.argv[1:] if (Path(d) / "umcub_probe/config.txt").is_file()]
    failed = 0
    for d in dirs:
        bad = check(d)
        for b in bad:
            print(f"{d}: {b}")
        failed += bool(bad)
    print(f"{len(dirs) - failed}/{len(dirs)} build trees: CMake and umcub.config agree")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
