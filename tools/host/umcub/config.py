"""The board configuration as the host tools see it: the values Kconfig generated
(umcub.kconfig), as a build tree has them or made here from a board and fragments."""
import json
import tempfile
from pathlib import Path

from . import kconfig
from .errors import UmcubError

ROOT = kconfig.ROOT
MODES = {1: "overwrite", 2: "swap-scratch", 4: "swap-offset", 5: "direct-xip", 6: "direct-xip-revert"}


def die(msg):
    raise UmcubError(msg)


class Config:
    """board: a build tree's generated configuration (a directory with
    umcub_config.json), or a board with a defconfig (name under boards/ or a
    directory), "" for none: generated here from it and the fragments conf."""

    def __init__(self, board, core=None, conf=()):
        self._tmp = None
        self.notes = []                 # for the user (new keys)
        self.config_dir = Path(board) if board else None
        if not board or not (self.config_dir / kconfig.JSON).is_file():
            if board and not kconfig.board_dir(board):
                die(f"{board}: no defconfig (board name under boards/ or a directory)")
            self._tmp = tempfile.TemporaryDirectory()
            self.notes = kconfig.generate(board, core, conf, self._tmp.name)
            self.config_dir = Path(self._tmp.name)
        data = json.loads((self.config_dir / kconfig.JSON).read_text())
        self.board_name = data["board"]
        self.board_dir = Path(data["board_dir"]) if data["board_dir"] else None
        self.v = data["values"]
        self.mcu = self.v["MCU"]
        self.family = "stm32" + self.mcu[5:7].lower()
        # the keys of the configuration (SIGNING, DEVICE, HOST), the defaults of signing
        self.keys = {name: Path(path) for name, path in data["keys"].items()}

    def __getitem__(self, k):
        return self.v.get(k, 0)

    def slot(self, image, slot):
        part = "PRIMARY" if slot == 0 else "SECONDARY"
        return self[f"IMG{image}_{part}_ADDR"], self[f"IMG{image}_{part}_SIZE"]

    def window(self, image, slot):
        """Flash window the application is linked into (umcub_app.cmake)."""
        addr, size = self.slot(image, slot)
        return addr + self["IMAGE_HEADER_SIZE"], size - self["IMAGE_HEADER_SIZE"] - self["TRAILER_RESERVE"]

    def images(self):
        return [i for i in (0, 1) if self[f"IMG{i}_PRIMARY_SIZE"]]

    def xip(self):
        return self["UPGRADE_MODE"] >= 5
