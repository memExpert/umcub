#!/usr/bin/env python3
"""umcub.kconfig on real configurations: what `umcub config generate` writes and what it warns about."""
import json
import os
import re
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/host"))
from umcub import kconfig  # noqa: E402
from umcub.config import Config  # noqa: E402
from umcub.errors import UmcubError  # noqa: E402

BOARD = "blackpill_f411ce"
PART = ROOT / "tests/configs/part/minimal_f4.conf"
bad = 0


def expect(what, got, want):
    global bad
    if got != want:
        bad += 1
        print(f"FAIL {what}: {got}, expected {want}")


def define(out, name):
    return re.search(rf"^#define UMCUB_CFG_{name}\s+(\S+)$", (out / kconfig.HEADER).read_text(), re.M).group(1)


def edited(notes):
    return any(n.startswith("new .config") for n in notes)


with tempfile.TemporaryDirectory() as tmp:
    t = Path(tmp)
    os.environ["UMCUB_KEYS_DIR"] = str(t / "keys")

    def fragment(name, *lines):
        (t / name).write_text("\n".join(lines) + "\n")
        return t / name

    # hex options without the 0x: the values they mean, in C too
    out = t / "hex"
    notes = kconfig.generate(BOARD, "", [fragment("hex.conf", "UMCUB_CFG_BOOT_SIZE=8000",
                                                  "UMCUB_CFG_IMG0_PRIMARY_ADDR=08020000")], out)
    expect("hex: size", define(out, "BOOT_SIZE"), "0x8000")
    expect("hex: address", define(out, "IMG0_PRIMARY_ADDR"), "0x08020000")
    expect("hex: derived", define(out, "BOOT_INFO_ADDR"), "0x8007F00")
    expect("a new signing key is told", any(n.startswith("new signing key") for n in notes), True)

    # no secondary slot: stops, unless the upgrades are given up
    nosec = ("UMCUB_CFG_IMG0_SECONDARY_ADDR=0x0", "UMCUB_CFG_IMG0_SECONDARY_SIZE=0x0")
    try:
        kconfig.generate(BOARD, "", [fragment("nosec.conf", *nosec)], t / "nosec")
        expect("no secondary slot stops", False, True)
    except UmcubError as e:
        expect("no secondary slot stops", "no secondary slot" in str(e), True)
    kconfig.generate(BOARD, "", [fragment("noup.conf", *nosec, "UMCUB_CFG_LAYOUT_NO_UPGRADE=y")], t / "noup")

    # no slots yet: nothing to sign, so no key; the key comes with the slots
    keys_dir = os.environ["UMCUB_KEYS_DIR"]
    os.environ["UMCUB_KEYS_DIR"] = str(t / "keys_no_slots")
    out = t / "no_slots"
    notes = kconfig.generate("", "", [], out)
    expect("no slots: told", any(n.startswith("no slots") for n in notes), True)
    expect("no slots: no key", (t / "keys_no_slots").exists(), False)
    notes = kconfig.generate("", "", [PART], out)
    expect("slots set: new key told", any(n.startswith("new signing key") for n in notes), True)
    expect("slots set: key made", (t / "keys_no_slots/signing.pem").is_file(), True)
    os.environ["UMCUB_KEYS_DIR"] = keys_dir

    # a tree without a board keeps its fragments through menuconfig / savedefconfig
    out = t / "noboard"
    kconfig.generate("", "", [PART], out)
    state = json.loads((out / kconfig.STATE).read_text())
    expect("no board: fragments", state["fragments"], [str(PART)])
    kconfig.savedefconfig(out, out / "saved")
    expect("no board: saved part", "UMCUB_CFG_PART_STM32F411CEUX=y" in (out / "saved").read_text(), True)
    expect("no board: same stamp", json.loads((out / kconfig.STATE).read_text())["stamp"], state["stamp"])

    # edits of .config: kept with the same inputs, told about when changed inputs drop them
    out, frag = t / "edit", fragment("edit.conf", "# one")
    kconfig.generate(BOARD, "", [frag], out)
    frag.write_text("# two\n")
    expect("no edits, changed inputs: no warning", edited(kconfig.generate(BOARD, "", [frag], out)), False)
    dot = out / ".config"
    dot.write_text(dot.read_text().replace("UMCUB_CFG_SIGNING_KEY_GENERATE=y", "# UMCUB_CFG_SIGNING_KEY_GENERATE is not set")
                   .replace("# UMCUB_CFG_SIGNING_KEY_DEV is not set", "UMCUB_CFG_SIGNING_KEY_DEV=y"))
    os.utime(dot, (time.time() + 2, time.time() + 2))        # newer than kconfig.done on any file system
    expect("edits, same inputs: no warning", edited(kconfig.generate(BOARD, "", [frag], out)), False)
    expect("edits, same inputs: kept", Config(out, "").keys["SIGNING"].name, "dev-ecdsa-p256.pem")
    frag.write_text("# three\n")
    notes = kconfig.generate(BOARD, "", [frag], out)
    expect("edits dropped: warning", edited(notes), True)
    expect("edits dropped: the option", any("SIGNING_KEY_DEV" in n for n in notes), True)
    expect("edits dropped: the old file", "SIGNING_KEY_DEV=y" in (out / ".config.old").read_text(), True)
    expect("edits dropped: key", Config(out, "").keys["SIGNING"].name, "signing.pem")
    frag.write_text("# four\n")
    expect("after that: no warning", edited(kconfig.generate(BOARD, "", [frag], out)), False)

# host library 0.5.3: a build tree's configuration is fixed - another core or fragments are an error, not ignored.
def refused(fn, words):
    """die() with `words` in its message."""
    try:
        fn()
    except Exception as e:   # noqa: BLE001  (die() raises UmcubError)
        return words in str(e)
    return False


with tempfile.TemporaryDirectory() as kd:
    kconfig.generate("nucleo_h755zi_q", "cm7", (), kd)
    expect("Config(build tree, another core): refused", refused(lambda: Config(kd, "cm4"), "not the cm4"), True)
    expect("Config(build tree, fragments): refused", refused(lambda: Config(kd, "cm7", ["x.conf"]), "no --conf"), True)
    expect("Config(build tree, its own core)", Config(kd, "cm7").board_name, "nucleo_h755zi_q")
# A key file on a file system without hard links (FAT, some SMB mounts) is still created, never replaced.
with tempfile.TemporaryDirectory() as kd:
    real_link = os.link

    def no_link(*_):
        raise PermissionError(1, "Operation not permitted")
    os.link = no_link
    try:
        made = kconfig._keygen(Path(kd) / "k.pem")
        again = kconfig._keygen(Path(kd) / "k.pem")
    finally:
        os.link = real_link
    expect("keygen without hard links: made once, kept", (made, again), (True, False))
    expect("keygen: no temporary file left", sorted(p.name for p in Path(kd).iterdir()), ["k.pem"])
# None and "" are the same core for the stamp of a build tree (a library caller passes None).
expect("stamp: core None and \"\"", kconfig._stamp("b", None, []), kconfig._stamp("b", "", []))

# DUALCORE_PER_CORE: two build trees; given the other one (--other-core-tree, CMake UMCUB_OTHER_CORE_TREE) the
# configure checks both layouts together. Without it, or before the other tree is configured: a note.
per_core = ROOT / "boards/nucleo_h755zi_q/variants/per_core.conf"
with tempfile.TemporaryDirectory() as kd:
    kd = Path(kd)
    kconfig.generate("nucleo_h755zi_q", "cm4", [per_core], kd / "cm4/umcub_kconfig")
    frag = kd / "cm7_on_cm4_boot.conf"
    frag.write_text("UMCUB_CFG_IMG0_SECONDARY_ADDR=0x08100000\n")
    expect("per core: a CM7 slot on the CM4 bootloader stops the configure",
           refused(lambda: kconfig.generate("nucleo_h755zi_q", "cm7", [per_core, frag], kd / "bad",
                                            other_core_tree=kd / "cm4"), "CM4 bootloader"), True)
    notes = kconfig.generate("nucleo_h755zi_q", "cm7", [per_core], kd / "ok", other_core_tree=kd / "cm4")
    expect("per core: apart, checked", any("checked against" in n for n in notes), True)
    notes = kconfig.generate("nucleo_h755zi_q", "cm7", [per_core], kd / "alone")
    expect("per core without the other tree: a note", any("UMCUB_OTHER_CORE_TREE" in n for n in notes), True)
    # the other tree must be the other core's, of the same part: else "checked" would name the wrong pair
    kconfig.generate("nucleo_h755zi_q", "cm7", [per_core], kd / "cm7b/umcub_kconfig")
    expect("per core: the other tree is of the same core: refused",
           refused(lambda: kconfig.generate("nucleo_h755zi_q", "cm7", [per_core], kd / "same",
                                            other_core_tree=kd / "cm7b"), "CM7 tree"), True)
    other = kd / "otherpart/umcub_kconfig"
    other.mkdir(parents=True)
    j = json.loads((kd / "cm4/umcub_kconfig" / kconfig.JSON).read_text())
    j["values"]["MCU"] = "STM32H747xx"
    (other / kconfig.JSON).write_text(json.dumps(j))
    expect("per core: the other tree is of another part: refused",
           refused(lambda: kconfig.generate("nucleo_h755zi_q", "cm7", [per_core], kd / "part",
                                            other_core_tree=kd / "otherpart"), "STM32H747xx"), True)
    # no core given: the build would default to the CM7 and skip the check - refused instead
    for core in ("", None):
        expect(f"per core without a core ({core!r}): refused",
               refused(lambda: kconfig.generate("nucleo_h755zi_q", core, [per_core], kd / "nocore",
                                                other_core_tree=kd / "cm4"), "UMCUB_CORE"), True)

print(f"kconfig: {'FAILED' if bad else 'all cases passed'}")
sys.exit(1 if bad else 0)
