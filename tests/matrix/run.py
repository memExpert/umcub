#!/usr/bin/env python3
"""One test of the build matrix (tests/CMakeLists.txt); logs go to <out>/<name>.*.log,
the line for the summary of tools/build/build_matrix.sh to <out>/summary/<name>.

  run.py --out DIR config "name|core|type|fragments|board"  bootloader: configure, build, no warnings
  run.py --out DIR preset NAME                              CMake preset: every target, a .map per .elf
  run.py --out DIR example "name|board|core|image"          example application: signed, ELF = objcopy
  run.py --out DIR app-library                              umcub_app_all.c compiled by itself (no CMake)
  run.py --out DIR noboard-app | no-slots                   configurations without a board
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
    # Kconfig: from the defconfig + fragments every time (a reused tree keeps its
    # .config otherwise, also across changes of the Kconfig files)
    (d / "umcub_kconfig/.config").unlink(missing_ok=True)
    run(["cmake", "-S", src, "-B", d, "-G", "Ninja"] + args, f"{d}.configure.log")
    run(["cmake", "--build", d], f"{d}.build.log")
    no_warnings(f"{d}.build.log")
    return d


def t_config(out, line):
    name, core, btype, conf, board = (line.split("#")[0].strip().split("|") + [""] * 5)[:5]
    board = board or DEFAULT_BOARD
    if board == "none":
        board = ""
    elif "/" in board:
        board = str(ROOT / board)
    # always given, also empty: a reused build tree keeps its cache
    args = [f"-DCMAKE_BUILD_TYPE={btype}", f"-DUMCUB_BOARD={board}", f"-DUMCUB_CORE={core}",
            "-DUMCUB_CONF=" + ";".join(overlay(f) for f in conf.split(",") if f)]
    d = build(out, name, args)
    if name == "custom-drivers" and re.search(r"fdcan\.c|/eth\.c", (d / "build.ninja").read_text()):
        raise Failed("the family FDCAN / ETH driver is built next to the board's")
    return name, flash_use(f"{d}.build.log")


def t_preset(out, preset):
    d = out / f"preset-{preset}"
    (d / "umcub_kconfig/.config").unlink(missing_ok=True)       # as in build()
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
    key = "tools/keys/dev-ecdsa-p256.pem"
    kc = d / "umcub_kconfig/keys.cmake"         # Kconfig board: the key of its configuration
    if kc.is_file():
        key = re.search(r'set\(UMCUB_SIGNING_KEY "([^"]*)"\)', kc.read_text()).group(1)
    run([ROOT / ".venv/bin/imgtool", "verify", "-k", key, d / f"{name}.signed.bin"], log)
    version = re.search(r"Image version: ([0-9.]+)\+", Path(log).read_text()).group(1)
    # CMake signs the ELF with `umcub image sign`; its ELF loader must see what
    # objcopy sees: signing the objcopy binary gives the same image.
    run(["arm-none-eabi-objcopy", "-O", "binary", d / f"{name}.elf", d / "objcopy.bin"], log, True)
    run([PY, "tools/host/umcub", "image", "sign", "--board", board] + (["--core", core] if core else []) +
        ["--image", image, "--version", version, d / "objcopy.bin", "-o", d / "objcopy"], log, True)
    run([PY, "tests/tools/compare_images.py", d / f"{name}.signed.bin", d / "objcopy.signed.bin"], log, True)
    return name, f"signed, Image version: {version}, ELF = objcopy"


def t_app_library(out):
    """umcub_app_all.c with only the documented include paths, the generated
    configuration and the CMSIS CORE_CMx define (docs/application.md, "Applications
    built without CMake"), also with clang (armclang in Keil MDK is clang based), and in
    every upgrade mode (static names are shared by all library sources there)."""
    def config(name, *conf):
        d = out / f"lib_config_{name}"
        args = [a for f in conf for a in ("--conf", f"tests/configs/mode/{f}.conf")]
        run([PY, "tools/host/umcub", "config", "generate", "--board", DEFAULT_BOARD, "--core", "cm7", "--out", d] + args,
            out / "app-library.log", True)
        return d

    incs = ["include", "config", "port/stm32h7/include", "port/include",
            "src/image/mcuboot/include", "third_party/mcuboot/boot/bootutil/include",
            "third_party/st/cmsis_device_h7/Include", "third_party/st/stm32h7xx_hal_driver/Inc",
            "third_party/cmsis_core/Include"]
    flags = ["-Os", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-DSTM32H755xx", "-DCORE_CM7"] + \
        [f"-I{i}" for i in incs] + ["-c", "lib/umcub_app/umcub_app_all.c"]
    cpu = ["-mcpu=cortex-m7", "-mfpu=fpv5-d16", "-mfloat-abi=hard"]
    log = out / "app-library.log"
    log.write_text("")
    default = f"-I{config('default')}"
    run(["arm-none-eabi-gcc", "-mthumb"] + cpu + [default] + flags + ["-o", out / "lib_gcc.o"], log, True)
    done = ["gcc"]
    if shutil.which("clang"):
        # newlib headers: <sysroot>/include; distribution toolchains print no
        # sysroot, there they sit next to libc.a's lib directory.
        inc = Path(subprocess.check_output(["arm-none-eabi-gcc", "-print-sysroot"], text=True).strip()) / "include"
        if not (inc / "string.h").is_file():
            libc = subprocess.check_output(["arm-none-eabi-gcc", "-print-file-name=libc.a"], text=True).strip()
            inc = Path(libc).parent.parent / "include"
        run(["clang", "--target=arm-none-eabi", "-Wno-unknown-attributes", "-isystem", inc] + cpu + [default] + flags +
            ["-o", out / "lib_clang.o"], log, True)
        done.append("clang")
    for m in ("overwrite", "swap_offset", "direct_xip", "direct_xip_revert"):
        run(["arm-none-eabi-gcc", "-mthumb"] + cpu + [f"-I{config(m, m)}"] + flags + ["-o", out / f"lib_{m}.o"], log, True)
    return "app-library", f"umcub_app_all.c: {' '.join(done)}, all upgrade modes"


def t_no_slots(out):
    """A first configure without a board or slots: menuconfig is there, the build says what is missing."""
    d = out / "no-slots"
    (d / "umcub_kconfig/.config").unlink(missing_ok=True)
    log = f"{d}.configure.log"
    run(["cmake", "-S", ROOT, "-B", d, "-G", "Ninja", "-DUMCUB_BOARD=", "-DUMCUB_CONF="], log)
    if "no slots set yet" not in Path(log).read_text():
        raise Failed(f"no note about the slots ({log})")
    r = subprocess.run(["cmake", "--build", d, "--target", "savedefconfig"], capture_output=True, text=True)
    if r.returncode:
        raise Failed(f"savedefconfig: {r.stdout}{r.stderr}")
    r = subprocess.run(["cmake", "--build", d], capture_output=True, text=True)
    if r.returncode == 0 or "no slots set yet" not in r.stdout:
        raise Failed(f"the build did not stop on the missing slots: {r.stdout}")
    return "no-slots", "configures, menuconfig targets there, the build stops with the hint"


def t_noboard_app(out):
    """An application without a board: the same fragments as its bootloader, a key of the part."""
    d = build(out, "noboard-app", ["-DUMCUB_BOARD=", f"-DUMCUB_CONF={ROOT}/tests/configs/part/minimal_f4.conf", "-DAPP_NODE_ADDR=none"],
              ROOT / "examples/blackpill_app")
    key = re.search(r'set\(UMCUB_SIGNING_KEY "([^"]*)"\)', (d / "umcub_kconfig/keys.cmake").read_text()).group(1)
    run([ROOT / ".venv/bin/imgtool", "verify", "-k", key, d / "blackpill_app.signed.bin"], f"{d}.verify.log")
    return "noboard-app", "examples/blackpill_app without a board: signed with the part's key"


def t_check(out, name, cmd, grep=None):
    log = out / f"{name}.log"
    run(cmd, log)
    lines = [l for l in log.read_text(errors="replace").splitlines() if l.strip() and (not grep or re.search(grep, l))]
    return name, lines[-1].strip() if lines else ""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("kind", choices=("config", "preset", "example", "app-library", "noboard-app", "no-slots", "check"))
    ap.add_argument("args", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    out = a.out.resolve()
    os.environ["UMCUB_KEYS_DIR"] = str(out / "keys")    # generated keys: not into the keys/ of the source tree
    (out / "summary").mkdir(parents=True, exist_ok=True)
    args = a.args[1:] if a.args[:1] == ["--"] else a.args
    try:
        if a.kind == "check":
            name, rest, grep = args[0], args[1:], None
            if rest[:1] == ["--grep"]:
                grep, rest = rest[1], rest[2:]
            label, text = t_check(out, name, rest[1:] if rest[:1] == ["--"] else rest, grep)
        else:
            label, text = {"config": t_config, "preset": t_preset, "example": t_example, "app-library": t_app_library, "noboard-app": t_noboard_app, "no-slots": t_no_slots}[a.kind](out, *args)
    except Failed as e:
        print(f"FAIL: {e}")
        sys.exit(1)
    line = f"{label:<24} {text}"
    (out / "summary" / label.replace(" ", "-")).write_text(line + "\n")
    print(line)


if __name__ == "__main__":
    main()
