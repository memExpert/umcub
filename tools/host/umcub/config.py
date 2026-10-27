"""The umcub configuration of a board as Python values: boards/<board>/umcub_config.h
(+ overlays) read through a C preprocessor, like cmake/umcub_config.cmake does it.

The preprocessor: `cc`, $UMCUB_CC, or the first one found of arm-none-eabi-gcc,
armclang (Keil MDK, also in the default install folders), gcc, clang.
"""
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from .errors import UmcubError

# The umcub checkout (boards/, config/, port/); $UMCUB_ROOT for an installed copy of this package.
ROOT = Path(os.environ.get("UMCUB_ROOT") or Path(__file__).resolve().parents[3])
MODES = {1: "overwrite", 2: "swap-scratch", 4: "swap-offset",
         5: "direct-xip", 6: "direct-xip-revert"}


def die(msg):
    raise UmcubError(msg)



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
