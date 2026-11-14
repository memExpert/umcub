#!/usr/bin/env python3
"""Versions and tags (tools/build/check_version.py): the bootloader's version lives in umcub_version.h and its tags
are vX.Y.Z; the host library's lives in tools/host/pyproject.toml and its tags are host-vX.Y.Z."""
import importlib.util
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("check_version", ROOT / "tools/build/check_version.py")
cv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cv)
bad = 0


def expect(what, ok, detail=""):
    global bad
    if not ok:
        bad += 1
        print(f"FAIL {what} {detail}")


boot, host = cv.boot_version(), cv.host_version()
for what, v in (("bootloader", boot), ("host library", host)):
    expect(f"{what} version X.Y.Z", re.fullmatch(r"\d+\.\d+\.\d+", v) is not None, v)

expect("v<bootloader> passes", cv.check(f"v{boot}") is None, cv.check(f"v{boot}"))
expect("another v tag fails", cv.check("v9.9.9") is not None)
expect("host-v<library> passes", cv.check(f"host-v{host}") is None, cv.check(f"host-v{host}"))
expect("another host-v tag fails", cv.check("host-v9.9.9") is not None)
expect("an unknown tag fails", cv.check("release-1") is not None)

print(f"versions: {'all cases passed' if not bad else f'{bad} failure(s)'}")
sys.exit(1 if bad else 0)
