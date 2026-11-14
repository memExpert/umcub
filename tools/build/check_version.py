#!/usr/bin/env python3
"""A tag matches its version: vX.Y.Z = the bootloader (src/util/include/umcub_version.h), host-vX.Y.Z = the host
library (tools/host/pyproject.toml). The Tags workflow runs it on a pushed tag: check_version.py <tag>..."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "src/util/include/umcub_version.h"
PYPROJECT = ROOT / "tools/host/pyproject.toml"


def boot_version():
    text = HEADER.read_text(encoding="utf-8")
    return ".".join(re.search(rf"#define UMCUB_VERSION_{p}\s+(\d+)", text)[1] for p in ("MAJOR", "MINOR", "PATCH"))


def host_version():
    return re.search(r'^version = "([^"]+)"', PYPROJECT.read_text(encoding="utf-8"), re.M)[1]


def check(tag):
    """None when the tag matches its version, else why not."""
    for prefix, what, version in (("host-v", "the host library", host_version), ("v", "the bootloader", boot_version)):
        if tag.startswith(prefix):
            v = version()
            return None if tag == prefix + v else f"{tag}: {what} is {v}"
    return f"{tag}: not v<bootloader> or host-v<host library>"


if __name__ == "__main__":
    errors = [e for e in map(check, sys.argv[1:]) if e]
    print("\n".join(errors) or "tags match their versions")
    sys.exit(1 if errors else 0)
