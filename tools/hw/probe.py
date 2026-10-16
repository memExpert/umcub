#!/usr/bin/env python3
"""SWD access for the bench tools, independent of the debug tool in use.

Backends (--probe or $UMCUB_PROBE):
  stm32cube  STM32_Programmer_CLI ($STM32_PROGRAMMER_CLI or PATH). Erasing an
             address range needs --sector-size (uniform sectors from --flash-base).
  pyocd      pyOCD (pip install pyocd): --target / $PYOCD_TARGET (e.g. stm32f103c8),
             --pack / $PYOCD_PACK (CMSIS-Pack file with the part).

  probe.py flash boot.hex [app.signed.hex ...]   program + verify, then reset
  probe.py flash app.bin@0x08008000              raw binary at an address
  probe.py read32 0x1FFFF7E8 3
  probe.py write32 0x20004F00 0x1 0x2
  probe.py erase 0x08008000 0x8000 | erase all
  probe.py reset [--hw]
--connect attach (default, running code keeps running) or under-reset.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time


class ProbeError(Exception):
    pass


class Stm32Cube:
    """STM32_Programmer_CLI, one call per operation."""

    def __init__(self, connect="attach", sector_size=None, flash_base=0x08000000, cli=None, **_):
        self.cli = cli or os.environ.get("STM32_PROGRAMMER_CLI") or shutil.which("STM32_Programmer_CLI")
        if not self.cli:
            raise ProbeError("STM32_Programmer_CLI not found: set $STM32_PROGRAMMER_CLI or add it to PATH")
        self.mode = {"attach": "HOTPLUG", "under-reset": "UR"}[connect]
        self.sector_size, self.flash_base = sector_size, flash_base

    def _run(self, *args, tries=5):
        out = ""
        for attempt in range(tries):
            r = subprocess.run([self.cli, "-c", "port=SWD", f"mode={self.mode}", *args],
                               capture_output=True, text=True)
            out = re.sub(r"\x1b\[[0-9;]*m", "", r.stdout + r.stderr)
            # Connect noise ("core ID", option database) is harmless when the command ran.
            bad = [l for l in out.splitlines() if l.startswith("Error") and "core ID" not in l
                   and "Database" not in l and "Failed to download data" not in l]
            if not bad and r.returncode == 0:
                return out
            time.sleep(0.5 + attempt)
        raise ProbeError(out[-600:])

    def read32(self, addr, n):
        vals = []
        for line in self._run("-r32", hex(addr), hex(4 * n)).splitlines():
            m = re.match(r"0x[0-9A-Fa-f]+\s*:\s*(.*)", line.strip())
            if m:
                vals += [int(v, 16) for v in m.group(1).split()]
        return vals[:n]

    def write32(self, addr, words):
        # Registers such as BSRR read back differently: the CLI then reports a
        # verify failure although the write happened - not checked here.
        subprocess.run([self.cli, "-c", "port=SWD", f"mode={self.mode}", "-w32", hex(addr),
                        *[hex(w) for w in words]], capture_output=True)

    def read_mem(self, addr, size):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "dump.bin")
            self._run("-u", hex(addr), hex(size), path)
            return open(path, "rb").read()

    def write_mem(self, addr, data):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "data.bin")
            open(path, "wb").write(data)
            self._run("-d", path, hex(addr))

    def program(self, path, addr=None):
        self._run("-d", path, *([hex(addr)] if addr is not None else []), "-v")

    def erase(self, addr=None, size=None):
        if addr is None:
            self._run("-e", "all")
            return
        if not self.sector_size:
            raise ProbeError("stm32cube: erasing a range needs --sector-size")
        first = (addr - self.flash_base) // self.sector_size
        last = (addr + size - 1 - self.flash_base) // self.sector_size
        self._run("-e", *[str(s) for s in range(first, last + 1)])

    def reset(self, hw=False):
        self._run("-hardRst" if hw else "-rst")

    def close(self):
        pass


class PyOcd:
    """pyOCD through its Python API; one session for all operations."""

    def __init__(self, connect="attach", target=None, pack=None, **_):
        try:
            from pyocd.core.helpers import ConnectHelper
        except ImportError:
            raise ProbeError("pyOCD is not installed (pip install pyocd)")
        target = target or os.environ.get("PYOCD_TARGET")
        pack = pack or os.environ.get("PYOCD_PACK")
        if not target:
            raise ProbeError("pyocd: give the part with --target or $PYOCD_TARGET")
        opts = {"target_override": target, "connect_mode": connect.replace("-", "_")}
        if pack:
            opts["pack"] = pack
        self.session = ConnectHelper.session_with_chosen_probe(return_first=True, options=opts)
        if self.session is None:
            raise ProbeError("pyocd: no debug probe found")
        self.session.open()
        self.target = self.session.board.target
        self.under_reset = connect == "under-reset"

    def read32(self, addr, n):
        return list(self.target.read_memory_block32(addr, n))

    def write32(self, addr, words):
        self.target.write_memory_block32(addr, list(words))

    def read_mem(self, addr, size):
        return bytes(self.target.read_memory_block8(addr, size))

    def write_mem(self, addr, data):
        self.target.write_memory_block8(addr, list(data))

    def _need_flash_algo(self):
        # A pack without a flash algorithm (ST's own DFPs) maps flash as ROM:
        # programming would then do nothing at all.
        if not any(r.is_flash and getattr(r, "algo", None) for r in self.target.memory_map):
            raise ProbeError("pyocd: the target has no flash algorithm - use a pack that has one "
                             "(e.g. Keil.STM32F1xx_DFP) or a built-in target")

    def program(self, path, addr=None):
        from pyocd.flash.file_programmer import FileProgrammer
        self._need_flash_algo()
        kw = {"base_address": addr} if addr is not None else {}
        FileProgrammer(self.session, chip_erase="sector", no_reset=True).program(path, **kw)

    def erase(self, addr=None, size=None):
        from pyocd.flash.eraser import FlashEraser
        self._need_flash_algo()
        if addr is None:
            FlashEraser(self.session, FlashEraser.Mode.CHIP).erase()
        else:
            FlashEraser(self.session, FlashEraser.Mode.SECTOR).erase([f"{addr:#x}+{size:#x}"])

    def reset(self, hw=False):
        from pyocd.core.target import Target
        self.target.reset(Target.ResetType.HARDWARE if hw else Target.ResetType.SYSRESETREQ)

    def close(self):
        if self.under_reset:
            self.target.resume()         # held since the connect: start the code now
        self.session.close()


BACKENDS = {"stm32cube": Stm32Cube, "pyocd": PyOcd}


def add_arguments(ap):
    """Probe options shared by the bench tools."""
    num = lambda s: int(s, 0)  # noqa: E731
    ap.add_argument("--probe", default=os.environ.get("UMCUB_PROBE", "stm32cube"), choices=sorted(BACKENDS))
    ap.add_argument("--connect", default="attach", choices=("attach", "under-reset"))
    ap.add_argument("--target", help="pyocd: part name")
    ap.add_argument("--pack", help="pyocd: CMSIS-Pack file")
    ap.add_argument("--sector-size", type=num, help="stm32cube: erase sector / page size")
    ap.add_argument("--flash-base", type=num, default=0x08000000)


def open_probe(a, connect=None):
    return BACKENDS[a.probe](connect=connect or a.connect, target=a.target, pack=a.pack,
                             sector_size=a.sector_size, flash_base=a.flash_base)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    add_arguments(ap)
    sub = ap.add_subparsers(dest="cmd", required=True)
    f = sub.add_parser("flash")
    f.add_argument("files", nargs="+", help="file.hex, or file.bin@ADDRESS")
    r = sub.add_parser("read32")
    r.add_argument("addr", type=lambda s: int(s, 0))
    r.add_argument("count", type=int, nargs="?", default=1)
    w = sub.add_parser("write32")
    w.add_argument("addr", type=lambda s: int(s, 0))
    w.add_argument("words", nargs="+", type=lambda s: int(s, 0))
    e = sub.add_parser("erase")
    e.add_argument("range", nargs="+", help="all, or ADDRESS SIZE")
    rs = sub.add_parser("reset")
    rs.add_argument("--hw", action="store_true", help="NRST instead of a system reset")
    a = ap.parse_args()

    try:
        p = open_probe(a)
        try:
            if a.cmd == "flash":
                for spec in a.files:
                    path, _, at = spec.partition("@")
                    p.program(path, int(at, 0) if at else None)
                    print(f"programmed {path}")
                p.reset()
            elif a.cmd == "read32":
                for i, v in enumerate(p.read32(a.addr, a.count)):
                    print(f"0x{a.addr + 4 * i:08x}: 0x{v:08x}")
            elif a.cmd == "write32":
                p.write32(a.addr, a.words)
            elif a.cmd == "erase":
                if a.range == ["all"]:
                    p.erase()
                else:
                    p.erase(int(a.range[0], 0), int(a.range[1], 0))
            else:
                p.reset(a.hw)
        finally:
            p.close()
    except ProbeError as err:
        sys.exit(f"probe: {err}")


if __name__ == "__main__":
    main()
