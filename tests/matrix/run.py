#!/usr/bin/env python3
"""One test of the build matrix (tests/CMakeLists.txt); logs go to <out>/<name>.*.log,
the line for the summary of tools/build/build_matrix.sh to <out>/summary/<name>.

  run.py --out DIR config "name|core|type|pre|post|board"   bootloader: configure, build, no warnings
  run.py --out DIR preset NAME                              CMake preset: every target, a .map per .elf
  run.py --out DIR example "name|board|core|image"          example application: signed, ELF = objcopy
  run.py --out DIR ide                                      umcub_app_all.c as an IDE project compiles it
  run.py --out DIR check NAME [--grep RE] -- COMMAND...     a check script: its last line (matching RE) is the summary
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PY = sys.executable
DEFAULT_BOARD = "nucleo_h755zi_q"


class Failed(Exception):
    pass


def run(cmd, log, append=False, cwd=ROOT):
    with open(log, "a" if append else "w") as f:
        r = subprocess.run([str(c) for c in cmd], cwd=cwd, stdout=f, stderr=subprocess.STDOUT)
    if r.returncode:
        raise Failed(f"{' '.join(str(c) for c in cmd[:3])} ... failed (see {log})")


def no_warnings(log):
    warn = [l for l in Path(log).read_text(errors="replace").splitlines() if "warning:" in l]
    if warn:
        raise Failed("warnings:\n" + "\n".join(warn[:5]))


def flash_use(log):
    m = re.search(r"^\s+FLASH:\s+(\d+ \w+)\s+\S+ \w+\s+(\S+)", Path(log).read_text(errors="replace"), re.M)
    return f"{m.group(1)} {m.group(2)}" if m else ""


def overlay(v):
    """tests/configs/<file>, or a board variant boards/<b>/variants/<file>."""
    return v if v.startswith("boards/") else f"tests/configs/{v}"


def build(out, name, args, src=ROOT):
    d = out / name
    run(["cmake", "-S", src, "-B", d, "-G", "Ninja"] + args, f"{d}.configure.log")
    run(["cmake", "--build", d], f"{d}.build.log")
    no_warnings(f"{d}.build.log")
    return d


def t_config(out, line):
    name, core, btype, pre, post, board = (line.split("#")[0].strip().split("|") + [""] * 6)[:6]
    board = board or DEFAULT_BOARD
    if "/" in board:
        board = str(ROOT / board)
    args = [f"-DCMAKE_BUILD_TYPE={btype}", f"-DUMCUB_BOARD={board}", f"-DUMCUB_CORE={core}"]
    if pre:
        args.append(f"-DUMCUB_CONFIG_PRE={overlay(pre)}")
    if post:
        args.append(f"-DUMCUB_CONFIG_POST={overlay(post)}")
    d = build(out, name, args)
    if name == "custom-drivers" and re.search(r"fdcan\.c|/eth\.c", (d / "build.ninja").read_text()):
        raise Failed("the family FDCAN / ETH driver is built next to the board's")
    return name, flash_use(f"{d}.build.log")


def t_preset(out, preset):
    d = out / f"preset-{preset}"
    run(["cmake", "--preset", preset, "-B", d], f"{d}.configure.log")
    run(["cmake", "--build", d], f"{d}.build.log")
    no_warnings(f"{d}.build.log")
    elfs = sorted(d.glob("*.elf")) + sorted(d.glob("examples/*/*.elf"))
    missing = [e.name for e in elfs if not e.with_suffix(".map").is_file()]
    if missing:
        raise Failed(f"no .map for {' '.join(missing)}")
    return f"preset {preset}", f"{len(elfs)} targets: {' '.join(e.name for e in elfs)}"


def t_example(out, line):
    name, board, core, image = line.split("|")
    d = build(out, name, [], ROOT / "examples" / name)
    log = f"{d}.verify.log"
    run([ROOT / ".venv/bin/imgtool", "verify", "-k", "tools/keys/dev-ecdsa-p256.pem", d / f"{name}.signed.bin"], log)
    version = re.search(r"Image version: ([0-9.]+)\+", Path(log).read_text()).group(1)
    # CMake signs the ELF with `umcub image sign`; its ELF loader must see what
    # objcopy sees: signing the objcopy binary gives the same image.
    run(["arm-none-eabi-objcopy", "-O", "binary", d / f"{name}.elf", d / "objcopy.bin"], log, True)
    run([PY, "tools/host/umcub", "image", "sign", "--board", board] + (["--core", core] if core else []) +
        ["--image", image, "--version", version, d / "objcopy.bin", "-o", d / "objcopy"], log, True)
    run([PY, "tests/tools/compare_images.py", d / f"{name}.signed.bin", d / "objcopy.signed.bin"], log, True)
    return name, f"signed, Image version: {version}, ELF = objcopy"


def t_ide(out):
    """umcub_app_all.c with only the documented include paths and the CMSIS
    CORE_CMx define (docs/application.md, "Using umcub from an IDE"), also with
    clang (armclang in Keil MDK is clang based), and in every upgrade mode
    (static names are shared by all library sources there)."""
    incs = ["include", "config", f"boards/{DEFAULT_BOARD}", "port/stm32h7/include", "port/include",
            "src/image/mcuboot/include", "third_party/mcuboot/boot/bootutil/include",
            "third_party/st/cmsis_device_h7/Include", "third_party/st/stm32h7xx_hal_driver/Inc",
            "third_party/cmsis_core/Include"]
    flags = ["-Os", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-DSTM32H755xx", "-DCORE_CM7"] + \
        [f"-I{i}" for i in incs] + ["-c", "lib/umcub_app/umcub_app_all.c"]
    cpu = ["-mcpu=cortex-m7", "-mfpu=fpv5-d16", "-mfloat-abi=hard"]
    log = out / "ide.log"
    run(["arm-none-eabi-gcc", "-mthumb"] + cpu + flags + ["-o", out / "ide_gcc.o"], log)
    done = ["gcc"]
    if shutil.which("clang"):
        # newlib headers: <sysroot>/include; distribution toolchains print no
        # sysroot, there they sit next to libc.a's lib directory.
        inc = Path(subprocess.check_output(["arm-none-eabi-gcc", "-print-sysroot"], text=True).strip()) / "include"
        if not (inc / "string.h").is_file():
            libc = subprocess.check_output(["arm-none-eabi-gcc", "-print-file-name=libc.a"], text=True).strip()
            inc = Path(libc).parent.parent / "include"
        run(["clang", "--target=arm-none-eabi", "-Wno-unknown-attributes", "-isystem", inc] + cpu + flags +
            ["-o", out / "ide_clang.o"], log, True)
        done.append("clang")
    for m in ("overwrite", "swap_offset", "direct_xip", "direct_xip_revert"):
        run(["arm-none-eabi-gcc", "-mthumb"] + cpu + [f'-DUMCUB_CONFIG_PRE="{ROOT}/tests/configs/mode/{m}.h"'] +
            flags + ["-o", out / f"ide_{m}.o"], log, True)
    return "ide-library", f"umcub_app_all.c: {' '.join(done)}, all upgrade modes"


def t_check(out, name, cmd, grep=None):
    log = out / f"{name}.log"
    run(cmd, log)
    lines = [l for l in log.read_text(errors="replace").splitlines() if l.strip() and (not grep or re.search(grep, l))]
    return name, lines[-1].strip() if lines else ""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("kind", choices=("config", "preset", "example", "ide", "check"))
    ap.add_argument("args", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    out = a.out.resolve()
    (out / "summary").mkdir(parents=True, exist_ok=True)
    args = a.args[1:] if a.args[:1] == ["--"] else a.args
    try:
        if a.kind == "check":
            name, rest, grep = args[0], args[1:], None
            if rest[:1] == ["--grep"]:
                grep, rest = rest[1], rest[2:]
            label, text = t_check(out, name, rest[1:] if rest[:1] == ["--"] else rest, grep)
        else:
            label, text = {"config": t_config, "preset": t_preset, "example": t_example, "ide": t_ide}[a.kind](out, *args)
    except Failed as e:
        print(f"FAIL: {e}")
        sys.exit(1)
    line = f"{label:<24} {text}"
    (out / "summary" / label.replace(" ", "-")).write_text(line + "\n")
    print(line)


if __name__ == "__main__":
    main()
