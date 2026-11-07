#!/usr/bin/env python3
"""Paths of the repository that clash on case-insensitive file systems
(Windows, macOS): two tracked files or directories whose names differ only in
case break a checkout, a clone and an unpacked source archive there."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    files = subprocess.run(["git", "ls-files"], cwd=ROOT, capture_output=True, text=True, check=True).stdout.split()
    paths = set()
    for f in files:
        parts = f.split("/")
        paths.update("/".join(parts[:i]) for i in range(1, len(parts) + 1))
    seen, clashes = {}, []
    for p in sorted(paths):
        other = seen.setdefault(p.lower(), p)
        if other != p:
            clashes.append(f"{other} <-> {p}")
    if clashes:
        sys.exit("paths that differ only in case (break Windows / macOS checkouts):\n  " + "\n  ".join(clashes))
    print(f"check_paths: {len(paths)} paths, no case clash")


if __name__ == "__main__":
    main()
