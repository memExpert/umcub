#!/usr/bin/env python3
"""umcub application images outside CMake (STM32CubeIDE, Keil MDK, IAR, ...).

  umcub_image.py info --board nucleo_h755zi_q --core cm7
      where to link the application (flash origin/length per image and slot),
      what RAM to keep free, ready-made IDE settings

  umcub_image.py sign --board nucleo_h755zi_q --core cm7 --image 0 --version 1.2.3 app.elf
      sign an ELF/AXF, Intel HEX or raw .bin -> app.signed.bin + app.signed.hex
      (and app.confirmed.{bin,hex} in direct-xip-revert mode)

The configuration is read from the same boards/<board>/umcub_config.h the
bootloader is built from, through a C preprocessor: --cc, $UMCUB_CC, or the
first one found of arm-none-eabi-gcc, armclang (Keil MDK, also in the default
install folders), gcc, clang. imgtool is imported from third_party/mcuboot
(pip install -r tools/requirements.txt), or pass --imgtool <executable>.
The signing arguments mirror umcub_sign_image() in cmake/umcub_imgtool.cmake.
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEV_KEY = ROOT / "tools/keys/dev-ecdsa-p256.pem"
DEV_DEVICE_KEY = ROOT / "tools/keys/dev-device-p256.pem"
MODES = {1: "overwrite", 2: "swap-scratch", 3: "swap-move", 4: "swap-offset",
         5: "direct-xip", 6: "direct-xip-revert"}


def die(msg):
    sys.exit(f"umcub_image: {msg}")


# --------------------------------------------------------------------------
# Configuration (same probing as cmake/umcub_config.cmake)
# --------------------------------------------------------------------------

def find_cc(explicit):
    cands = [explicit, os.environ.get("UMCUB_CC"), "arm-none-eabi-gcc", "armclang"]
    if os.name == "nt":
        for base in (r"C:\Keil_v5", os.path.expandvars(r"%LOCALAPPDATA%\Keil_v5")):
            cands.append(os.path.join(base, r"ARM\ARMCLANG\bin\armclang.exe"))
    cands += ["gcc", "clang", "cc"]
    for c in cands:
        if c and (shutil.which(c) or os.path.isfile(c)):
            return shutil.which(c) or c
    die("no C preprocessor found (needed to read umcub_config.h): pass --cc <arm-none-eabi-gcc|armclang|gcc>")


class Config:
    def __init__(self, board, core, pre, post, cc):
        bdir = Path(board)
        if not (bdir / "umcub_config.h").is_file():
            bdir = ROOT / "boards" / board
        if not (bdir / "umcub_config.h").is_file():
            die(f"{board}: no umcub_config.h (board name under boards/ or a directory)")
        self.board_dir = bdir.resolve()
        self.cc = find_cc(cc)
        self.defs = []
        if core:
            self.defs += [f"UMCUB_CORE_{core.upper()}=1", f"CORE_{core.upper()}"]
        for name, f in (("UMCUB_CONFIG_PRE", pre), ("UMCUB_CONFIG_POST", post)):
            if f:
                self.defs.append(f'{name}="{Path(f).resolve().as_posix()}"')
        self.incs = [ROOT / "config", self.board_dir]

        out = self._cpp('#include "umcub_config_types.h"\n#ifdef UMCUB_CONFIG_PRE\n#include UMCUB_CONFIG_PRE\n'
                        '#endif\n#include "umcub_config.h"\n#ifdef UMCUB_CONFIG_POST\n#include UMCUB_CONFIG_POST\n'
                        '#endif\n@@MCU@@ UMCUB_CFG_MCU\n')
        m = re.search(r"@@MCU@@ (STM32[A-Za-z0-9_]+)", out)
        if not m:
            die("UMCUB_CFG_MCU must be set to a CMSIS device define (e.g. STM32H755xx)")
        self.mcu = m.group(1)
        self.family = "stm32" + self.mcu[5:7].lower()
        fam_inc = ROOT / "port" / self.family / "include"
        if not fam_inc.is_dir():
            die(f"no port for {self.family} ({self.mcu}) yet")
        self.defs.append(self.mcu)
        self.incs.append(fam_inc)

        names = set()
        for f in (ROOT / "config/umcub_config_template.h", ROOT / "config/umcub_config_defaults.h",
                  fam_inc / "umcub_family_defaults.h", self.board_dir / "umcub_config.h"):
            names |= set(re.findall(r"#\s*define\s+(UMCUB_CFG_[A-Z0-9_]+)", f.read_text()))
        probe = '#include "umcub_cfg.h"\n' + "".join(f'@@ "{n}" {n}\n' for n in sorted(names))
        probe += '@@ "FAMILY_WRITE_ALIGN" UMCUB_FAMILY_WRITE_ALIGN\n@@ "FAMILY_MIN_SECTOR" UMCUB_FAMILY_MIN_SECTOR\n'
        self.v = {}
        for line in self._cpp(probe).splitlines():
            m = re.match(r'@@ "([A-Z0-9_]+)" (.*)$', line)
            if m and m.group(2).strip() != m.group(1):
                self.v[m.group(1).removeprefix("UMCUB_CFG_")] = self._eval(m.group(2).strip())

    def _cpp(self, text):
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / "probe.c"
            src.write_text(text)
            cmd = [self.cc]
            if "armclang" in Path(self.cc).name.lower():
                cmd.append("--target=arm-arm-none-eabi")
            cmd += ["-E", "-P", "-x", "c"] + [f"-D{d_}" for d_ in self.defs] + [f"-I{i}" for i in self.incs]
            r = subprocess.run(cmd + [str(src)], capture_output=True, text=True)
        if r.returncode:
            die(f"configuration error ({self.cc}):\n{r.stderr}")
        return r.stdout

    @staticmethod
    def _eval(text):
        e = re.sub(r"\b(0[xX][0-9a-fA-F]+|\d+)[uUlL]+\b", r"\1", text)
        if re.fullmatch(r"[-+*/%()<>&|^~ \t0-9a-fA-FxX]+", e) and "&&" not in e and "||" not in e:
            try:
                return int(eval(e.replace("/", "//"), {"__builtins__": {}}))
            except Exception:
                pass
        return text

    def __getitem__(self, k):
        return self.v.get(k, 0)

    def slot(self, image, slot):
        part = "PRIMARY" if slot == 0 else "SECONDARY"
        return self[f"IMG{image}_{part}_ADDR"], self[f"IMG{image}_{part}_SIZE"]

    def window(self, image, slot):
        """Flash window the application is linked into (umcub_app.cmake)."""
        addr, size = self.slot(image, slot)
        hdr = self["IMAGE_HEADER_SIZE"]
        reserve = self.v.get("TRAILER_RESERVE", 0x2000)
        return addr + hdr, size - hdr - reserve

    def images(self):
        return [i for i in (0, 1) if self[f"IMG{i}_PRIMARY_SIZE"]]

    def xip(self):
        return self["UPGRADE_MODE"] >= 5


# --------------------------------------------------------------------------
# Input images
# --------------------------------------------------------------------------

def load_segments(path):
    data = Path(path).read_bytes()
    ext = Path(path).suffix.lower()
    if data[:4] == b"\x7fELF":
        if data[4] != 1 or data[5] != 1:
            die(f"{path}: only 32-bit little-endian ELF")
        # Like objcopy -O binary: allocated sections with contents, placed at
        # their load address (segment p_paddr + offset within the segment).
        phoff, shoff = struct.unpack_from("<II", data, 0x1C)
        phentsize, phnum, shentsize, shnum = struct.unpack_from("<HHHH", data, 0x2A)
        loads = []
        for i in range(phnum):
            p_type, _off, p_vaddr, p_paddr, _filesz, p_memsz = struct.unpack_from("<6I", data, phoff + i * phentsize)
            if p_type == 1:                         # PT_LOAD
                loads.append((p_vaddr, p_paddr, p_memsz))
        segs = []
        for i in range(shnum):
            _name, sh_type, sh_flags, sh_addr, sh_off, sh_size = struct.unpack_from("<6I", data, shoff + i * shentsize)
            if not (sh_flags & 0x2) or sh_type == 8 or not sh_size:     # !SHF_ALLOC, SHT_NOBITS, empty
                continue
            lma = next((pa + sh_addr - va for va, pa, ms in loads if va <= sh_addr < va + ms), sh_addr)
            segs.append((lma, data[sh_off:sh_off + sh_size]))
        if not segs:
            die(f"{path}: no loadable sections")
        return segs
    if ext in (".hex", ".ihex"):
        try:
            from intelhex import IntelHex
        except ImportError:
            die("intelhex is needed for .hex input (pip install -r tools/requirements.txt)")
        ih = IntelHex(str(path))
        return [(a, ih.tobinstr(start=a, end=b - 1)) for a, b in ih.segments()]
    if ext == ".bin":
        return [(None, data)]
    die(f"{path}: unknown input format (ELF/AXF, .hex or .bin)")


def flatten(segs, origin, length, path):
    if segs[0][0] is None:                          # raw .bin: assumed linked at origin
        segs = [(origin, segs[0][1])]
    lo = min(a for a, _ in segs)
    hi = max(a + len(d) for a, d in segs)
    for a, d in segs:
        if a < origin or a + len(d) > origin + length:
            die(f"{path}: data at 0x{a:08X}..0x{a + len(d):08X} is outside the slot window "
                f"0x{origin:08X}..0x{origin + length:08X} - is the flash origin/length in the linker "
                f"settings right? (umcub_image.py info)")
    if lo != origin:
        die(f"{path}: image starts at 0x{lo:08X}, the vector table must be at 0x{origin:08X}")
    img = bytearray(hi - lo)                        # gaps 0x00, like objcopy -O binary
    for a, d in segs:
        img[a - lo:a - lo + len(d)] = d
    if len(img) >= 8:
        sp, reset = struct.unpack_from("<II", img, 0)
        if not (origin <= (reset & ~1) < origin + len(img)) or not reset & 1:
            print(f"warning: reset vector 0x{reset:08X} is not a Thumb address inside the image", file=sys.stderr)
    return bytes(img)


def write_hex(path, data, addr):
    out = []

    def rec(t, a, payload):
        b = bytes([len(payload), (a >> 8) & 0xFF, a & 0xFF, t]) + payload
        out.append(":" + b.hex().upper() + f"{(-sum(b)) & 0xFF:02X}")

    upper = None
    for off in range(0, len(data), 16):
        a = addr + off
        if a >> 16 != upper:
            upper = a >> 16
            rec(4, 0, struct.pack(">H", upper))
        rec(0, a & 0xFFFF, data[off:off + 16])
    rec(1, 0, b"")
    Path(path).write_text("\n".join(out) + "\n")


# --------------------------------------------------------------------------
# Commands
# --------------------------------------------------------------------------

def run_imgtool(args, exe):
    if exe:
        r = subprocess.run([exe] + args)
        if r.returncode:
            die("imgtool failed")
        return
    sys.path.insert(0, str(ROOT / "third_party/mcuboot/scripts"))
    try:
        from imgtool.main import imgtool
    except ImportError as e:
        die(f"cannot import imgtool ({e}): pip install -r tools/requirements.txt, or pass --imgtool")
    try:
        imgtool.main(args=args, prog_name="imgtool", standalone_mode=False)
    except Exception as e:                          # click.ClickException and friends
        die(f"imgtool: {e}")


def cmd_sign(cfg, a):
    mode = cfg["UPGRADE_MODE"]
    slot = a.slot if cfg.xip() else 0               # swap/overwrite: always linked for the primary
    slot_addr, slot_size = cfg.slot(a.image, slot)
    if not slot_size:
        die(f"image {a.image} slot {slot} is not configured")
    if not cfg.xip():                               # secondary may be smaller (swap-move)
        sec = cfg.slot(a.image, 1)[1]
        if sec and sec < slot_size:
            slot_size = sec
    origin, length = cfg.window(a.image, slot)
    img = flatten(load_segments(a.input), origin, length, a.input)

    align = cfg["FAMILY_WRITE_ALIGN"]
    args = ["sign", "--version", a.version, "--header-size", str(cfg["IMAGE_HEADER_SIZE"]), "--pad-header",
            "--slot-size", str(slot_size), "--align", str(align), "--max-align", str(align)]
    if cfg["SIGNATURE"] == 1:
        key = Path(a.key) if a.key else DEV_KEY
        if key.resolve() == DEV_KEY.resolve():
            print("warning: signing with the development key from the repository", file=sys.stderr)
        args += ["-k", str(key)]
    if mode == 1:
        args.append("--overwrite-only")
    elif mode <= 4:                                 # trailer sized by the largest slot of any image
        biggest = max(cfg.slot(i, s)[1] for i in (0, 1) for s in (0, 1))
        args += ["--max-sectors", str(biggest // cfg["FAMILY_MIN_SECTOR"])]
    else:
        args += ["--rom-fixed", hex(slot_addr)]
    if cfg["BOARD_TYPE"]:                           # signed TLV checked by the bootloader
        args += ["--custom-tlv", "0xa0", "0x" + int(cfg["BOARD_TYPE"]).to_bytes(4, "little").hex()]
    if a.confirm:
        args.append("--confirm")
    if a.pad:
        args.append("--pad")
    if a.depends:
        args += ["--dependencies", a.depends]

    base = Path(a.output) if a.output else Path(a.input).with_suffix("")
    with tempfile.TemporaryDirectory() as d:
        raw = Path(d) / "app.bin"
        raw.write_bytes(img)
        outs = [(args, base.with_name(base.name + ".signed"))]
        if mode == 6 and not a.confirm:
            outs.append((args + ["--confirm", "--pad"], base.with_name(base.name + ".confirmed")))
        for sargs, out in outs:
            run_imgtool(sargs + [str(raw), str(out) + ".bin"], a.imgtool)
            write_hex(str(out) + ".hex", Path(str(out) + ".bin").read_bytes(), slot_addr)
            print(f"{out}.bin / .hex: image {a.image} slot {slot} version {a.version} at 0x{slot_addr:08X} "
                  f"({len(img)} bytes of code, {MODES.get(mode, mode)})")
        if cfg["ENCRYPT_IMAGES"]:                   # for updates; the plain .signed.* is for programmers
            dkey = Path(a.encrypt_key) if a.encrypt_key else DEV_DEVICE_KEY
            if dkey.resolve() == DEV_DEVICE_KEY.resolve():
                print("warning: encrypting with the development device key from the repository", file=sys.stderr)
            out = base.with_name(base.name + ".encrypted.bin")
            run_imgtool(args + ["--encrypt", str(dkey), str(raw), str(out)], a.imgtool)
            print(f"{out}: the same image encrypted with {dkey.name} (SMP / DFU / application updates)")


def cmd_info(cfg, a):
    mode = cfg["UPGRADE_MODE"]
    print(f"{cfg.board_dir.name}{' / ' + a.core if a.core else ''}: {cfg.mcu}, {MODES.get(mode, mode)}, "
          f"header 0x{cfg['IMAGE_HEADER_SIZE']:X}, write align {cfg['FAMILY_WRITE_ALIGN']}")
    single_boot = cfg["DUALCORE_MODE"] == 1
    for img in cfg.images():
        core = ("cm7", "cm4")[img] if single_boot else a.core
        for slot in ((0, 1) if cfg.xip() else (0,)):
            origin, length = cfg.window(img, slot)
            addr, size = cfg.slot(img, slot)
            print(f"\nimage {img}{' (' + core.upper() + ' application)' if single_boot else ''}"
                  f"{' slot ' + str(slot) if cfg.xip() else ''}: slot 0x{addr:08X} size 0x{size:X}")
            print(f"  link at   ORIGIN 0x{origin:08X}  LENGTH 0x{length:X}   (vector table = ORIGIN)")
            print(f"  GCC .ld   FLASH (rx) : ORIGIN = 0x{origin:08X}, LENGTH = 0x{length:X}")
            print(f"  Keil      Target > IROM1: Start 0x{origin:08X}  Size 0x{length:X}")
            print(f"  sign      umcub_image.py sign --board {a.board}{' --core ' + core if core else ''} "
                  f"--image {img}{' --slot ' + str(slot) if cfg.xip() else ''} --version <x.y.z> <app.elf|.axf|.hex>")
    shared = cfg["SHARED_RAM_ADDR"]
    print(f"\nkeep free: 0x{shared:08X}..0x{shared + 256:08X} (bootloader handoff, kept over reset)")
    if cfg["DUALCORE_MODE"]:
        print("           dual-core: keep the handoff areas of both cores free (H7: 0x3800FE00..0x38010000)")
    print("VTOR:      set by the bootloader - leave USER_VECT_TAB_ADDRESS undefined in system_stm32*.c")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("info", "sign"):
        p = sub.add_parser(name)
        p.add_argument("--board", required=True, help="board name under boards/ or a directory with umcub_config.h")
        p.add_argument("--core", choices=("cm7", "cm4"), help="dual-core parts: core the application runs on")
        p.add_argument("--pre", help="UMCUB_CONFIG_PRE overlay (same as the bootloader build)")
        p.add_argument("--post", help="UMCUB_CONFIG_POST overlay")
        p.add_argument("--cc", help="C compiler used as preprocessor (arm-none-eabi-gcc, armclang, gcc)")
        if name == "sign":
            p.add_argument("input", help="ELF/AXF, Intel HEX or raw .bin linked for the slot")
            p.add_argument("--image", type=int, default=0)
            p.add_argument("--slot", type=int, default=0, help="direct-xip: slot the image is linked for")
            p.add_argument("--version", default="0.0.0", help="x.y.z[+build]")
            p.add_argument("--key", help=f"signing key (default {DEV_KEY.relative_to(ROOT)})")
            p.add_argument("--encrypt-key", help="UMCUB_CFG_ENCRYPT_IMAGES: device key for <out>.encrypted.bin "
                                                  f"(default {DEV_DEVICE_KEY.relative_to(ROOT)})")
            p.add_argument("--confirm", action="store_true", help="mark the image confirmed (image_ok)")
            p.add_argument("--pad", action="store_true", help="pad to the slot size with a trailer")
            p.add_argument("--depends", help='multi-image dependency, e.g. "(1,1.0.0)"')
            p.add_argument("-o", "--output", help="output base name (default: input without extension)")
            p.add_argument("--imgtool", help="imgtool executable instead of the bundled module")
    a = ap.parse_args()
    cfg = Config(a.board, a.core, a.pre, a.post, a.cc)
    (cmd_sign if a.cmd == "sign" else cmd_info)(cfg, a)


if __name__ == "__main__":
    main()
