"""The umcub configuration from Kconfig (esp-idf-kconfig): a board's defconfig
plus configuration fragments -> .config and what the rest of the tree reads:
umcub_config.h (C), umcub_config.cmake (CMake), umcub_config.json (host tools).

Every option is in them, also one whose dependencies are not met (with its
default, or off), plus the values derived from the options (derived()).
Symbols are written as UMCUB_CFG_<NAME> (.config lines, #defines, CMake variables).
In the header: bool -> 0 / 1, int and hex as numbers, pins ("PA9", "PD12:7"
with the alternate function) -> UMCUB_PIN(), IPv4 addresses -> UMCUB_IP4(),
the MCU as the CMSIS device define, other strings as C strings. Choice members
are left out (a hidden int carries the choice).

A build tree keeps its .config while the defconfig and the fragments stay the
same (menuconfig changes survive a reconfigure); when they change, .config is
made again from them.
"""
import hashlib
import json
import os
import re
from pathlib import Path

from . import layout
from .errors import UmcubError

ROOT = Path(os.environ.get("UMCUB_ROOT") or Path(__file__).resolve().parents[3])
PREFIX = "UMCUB_CFG_"
STATE = "kconfig.json"          # how the build tree's .config was made
HEADER = "umcub_config.h"
JSON = "umcub_config.json"
CMAKE = "umcub_config.cmake"
DONE = "kconfig.done"           # written last by generate()
IP_NAMES = {"ETH_IP", "ETH_NETMASK"}
RAW_NAMES = {"MCU"}             # C tokens, not strings
# Kconfig helpers that stay out of the header: what the part has, pin sets,
# lines and families (port/kconfig/*.kconfig, port/Kconfig, Kconfig).
INTERNAL = re.compile(r"(PIN|PINSET|HAS)_\w+|FAMILY_STM32\w+|MCU_STM32\w+|BUILD_CORE|CORE_CM4|MCU_DUALCORE|LINK_SECURE"
                      r"|KEYS_DIR|(SIGNING|DEVICE|ADMIN)_KEY_\w+")
# The keys (config/Kconfig "Keys"): generated file name, development key of the repository.
KEYS = {"SIGNING": ("signing.pem", "dev-ecdsa-p256.pem"),
        "DEVICE": ("device.pem", "dev-device-p256.pem"),
        "ADMIN": ("admin.pem", "dev-admin-p256.pem")}
# Slots: 0 when their options are off (the board's defaults hold for the slots in use).
SLOT = re.compile(r"(IMG\d_(PRIMARY|SECONDARY)|SCRATCH)_(ADDR|SIZE)")
# UMCUB_CFG_CMD_TABLE: its actions and the groups of inspection commands.
CMD_ACTIONS = {"BOOT_APP", "STAY", "RESET", "INFO", "VERIFY", "HASH", "READ"}
CMD_GROUPS = {"+inspect": "UMCUB_DEFAULT_CMD_INSPECT", "+inspect_read": "UMCUB_CMD_INSPECT_DEFAULTS"}
# The RMII pin menus (port/kconfig/eth_rmii.kconfig) -> UMCUB_CFG_ETH_RMII_PINS, in this order.
RMII = ["REF_CLK", "MDIO", "CRS_DV", "MDC", "RXD0", "RXD1", "TX_EN", "TXD0", "TXD1"]


def die(msg):
    raise UmcubError(msg)


def board_dir(board):
    """A board name under boards/ or a board directory (with a defconfig)."""
    if not board:
        return None
    d = Path(board)
    if not (d / "defconfig").is_file():
        d = ROOT / "boards" / board
    return d.resolve() if (d / "defconfig").is_file() else None


def _rec_invalidate(item):
    """In place of esp-kconfiglib's: that one walks every path through the
    dependencies, seconds per assignment with the part and pin menus (minutes to
    load a .config, also in menuconfig). Every item once does the same."""
    seen, todo = {id(item)}, [item]
    while todo:
        cur = todo.pop()
        cur._invalidate()
        for dep in cur._dependents:
            if id(dep) not in seen:
                seen.add(id(dep))
                todo.append(dep)


def _kconfig(bdir, core):
    os.environ.update({"CONFIG_": PREFIX, "UMCUB_ROOT": ROOT.as_posix(),
                       "UMCUB_BOARD_DIR": Path(bdir).as_posix() if bdir else (ROOT / "boards/.none").as_posix(),
                       "UMCUB_CORE": core or ""})
    try:
        import esp_kconfiglib.core as k
    except ImportError:
        die("esp-idf-kconfig is not installed (pip install esp-idf-kconfig; tools/setup.sh does it)")
    k.Symbol._rec_invalidate = k.Choice._rec_invalidate = _rec_invalidate
    try:
        return k, k.Kconfig(str(ROOT / "Kconfig"), warn_to_stderr=False, info=False)
    except k.KconfigError as e:
        die(f"Kconfig: {e}")


def _assignments(path):
    """(line, name, value) of every assignment in a defconfig / fragment."""
    out = []
    for n, line in enumerate(Path(path).read_text().splitlines(), 1):
        line = line.strip()
        m = re.fullmatch(rf"{PREFIX}(\w+)=(.*)", line)
        if m:
            v = m.group(2)
            out.append((n, m.group(1), v[1:-1] if v.startswith('"') and v.endswith('"') else v))
            continue
        m = re.fullmatch(rf"# {PREFIX}(\w+) is not set", line)
        if m:
            out.append((n, m.group(1), "n"))
        elif line and not line.startswith("#"):
            die(f"{path}:{n}: not a {PREFIX}<NAME>=<value> line: {line}")
    return out


def _check_applied(kconf, inputs):
    """Every assignment must have taken effect: a symbol that does not exist or a
    value its dependencies or range do not allow is an error, not a silent default.
    A later assignment overrides an earlier one, of the symbol or of its choice,
    also by switching off what it depends on (a fragment that turns SMP off
    drops the defconfig's SMP_MTU)."""
    import esp_kconfiglib.core as k
    last, later, errs = {}, {}, []
    for i, f in enumerate(inputs):
        for n, name, v in _assignments(f):
            sym = kconf.syms.get(name)
            if sym is None or not sym.nodes:
                errs.append(f"{f}:{n}: {PREFIX}{name}: no such option")
                continue
            key = sym.choice if sym.choice is not None and v == "y" else sym
            last[key] = (i, f, n, sym, v)
            later[sym] = i                      # the last input that assigns sym

    def overridden(sym, i, seen):
        """A dependency of sym (transitively) assigned by an input after i."""
        for d in k.expr_items(sym.direct_dep):     # symbols and choices (with their own dependencies)
            if d in seen or not hasattr(d, "direct_dep"):
                continue
            seen.add(d)
            choice = getattr(d, "choice", None)
            if later.get(d, -1) > i or any(later.get(m, -1) > i for m in getattr(choice, "syms", ())) \
                    or overridden(d, i, seen):
                return True
        return False

    for i, f, n, sym, v in last.values():
        got = sym.str_value
        if got != v and not (v.lower().startswith("0x") and got.lower() == v.lower()):
            if got in ("", "n") and k.expr_value(sym.direct_dep) == 0 and overridden(sym, i, set()):
                continue
            dep = " (depends on " + k.expr_str(sym.direct_dep) + ")" if got in ("", "n") else ""
            errs.append(f"{f}:{n}: {PREFIX}{sym.name}={v} not applied, it is {got or 'unset'}{dep}")
    if errs:
        die("configuration:\n  " + "\n  ".join(errs))


def _stamp(bdir, core, inputs):
    h = hashlib.sha256(f"{bdir}|{core}".encode())
    for f in inputs:
        h.update(str(f).encode() + b"\0" + Path(f).read_bytes())
    return h.hexdigest()


def load(board, core="", fragments=(), out_dir=None):
    """The configuration of a board (+ fragments); with out_dir, the build tree's
    .config when it was made from the same inputs. Returns (kconf, bdir, inputs)."""
    bdir = board_dir(board)
    if board and bdir is None:
        die(f"{board}: no defconfig (board name under boards/ or a board directory)")
    # no board: the defaults (part, transports, layout) + the fragments, then menuconfig
    inputs = ([bdir / "defconfig"] if bdir else []) + [Path(f).resolve() for f in fragments]
    for f in inputs:
        if not f.is_file():
            die(f"{f}: not found")
    k, kconf = _kconfig(bdir, core)
    dot = Path(out_dir) / ".config" if out_dir else None
    stamp = _stamp(bdir, core, inputs)
    state = _state(out_dir)
    if dot and dot.is_file() and state.get("stamp") == stamp:
        kconf.load_config(str(dot))
    else:
        for i, f in enumerate(inputs):
            kconf.load_config(str(f), replace=(i == 0))
        _check_applied(kconf, inputs)
    if out_dir:
        Path(out_dir).mkdir(parents=True, exist_ok=True)
        (Path(out_dir) / STATE).write_text(json.dumps(
            {"board": str(bdir) if bdir else "", "core": core or "", "fragments": [str(f) for f in inputs[1:]],
             "stamp": stamp}, indent=1))
    return kconf, bdir, inputs


def _state(out_dir):
    try:
        return json.loads((Path(out_dir) / STATE).read_text())
    except (OSError, ValueError, TypeError):
        return {}


def _pin(name, text):
    if not text:
        return "UMCUB_PIN_NONE"
    m = re.fullmatch(r"P([A-K])(\d{1,2})(?::(\d{1,2}))?", text.strip().upper())
    if not m or int(m.group(2)) > 15 or (m.group(3) and int(m.group(3)) > 15):
        die(f"{PREFIX}{name}: \"{text}\" is not a pin (PA9, or PD12:7 with the alternate function)")
    af = f", {int(m.group(3))}" if m.group(3) else ""
    return f"UMCUB_PIN('{m.group(1)}', {int(m.group(2))}{af})"


def _ip(name, text):
    parts = text.split(".")
    if len(parts) != 4 or not all(p.isdigit() and int(p) < 256 for p in parts):
        die(f"{PREFIX}{name}: \"{text}\" is not an IPv4 address")
    return f"UMCUB_IP4({', '.join(str(int(p)) for p in parts)})"


def _cmd_table(text):
    """'a=BOOT_APP hello=USER1 +inspect' -> the C table."""
    out = []
    for item in text.split():
        name, _, action = item.partition("=")
        user = re.fullmatch(r"USER(\d+)", action)
        if item in CMD_GROUPS:
            out.append(CMD_GROUPS[item])
        elif name and user:
            out.append(f'UMCUB_CMD("{name}", UMCUB_CMD_USER({user.group(1)}))')
        elif name and action in CMD_ACTIONS:
            out.append(f'UMCUB_CMD("{name}", UMCUB_CMD_{action})')
        else:
            die(f"{PREFIX}CMD_TABLE: \"{item}\": name=ACTION expected ({', '.join(sorted(CMD_ACTIONS))}, USER<n>)"
                f" or {' / '.join(CMD_GROUPS)}")
    return " ".join(out)


def _off_value(k, sym):
    """The value of an option whose dependencies are not met: its first default
    that holds, nothing for what has none."""
    if SLOT.fullmatch(sym.name):
        return "0"
    for node in sym.nodes:
        for val, cond in node.orig_defaults:    # as written: .defaults also carry the dependencies
            if k.expr_value(cond):
                return val.str_value if not isinstance(val, tuple) else ""
    return "n" if sym.type == k.BOOL else "0" if sym.type in (k.INT, k.HEX) else ""


def _c_text(k, sym, v):
    """A value as C text, None: no value."""
    if sym.type == k.BOOL:
        return "1" if v == "y" else "0"
    if sym.type in (k.INT, k.HEX):
        return v if v != "" else None
    if sym.name in RAW_NAMES:
        return v or None
    if sym.name.endswith("_PINS"):
        return ", ".join(_pin(sym.name, p) for p in v.split())
    if sym.name.endswith("_PIN"):
        return _pin(sym.name, v)
    if sym.name in IP_NAMES:
        return _ip(sym.name, v)
    return json.dumps(v)


def values(kconf):
    """{NAME: C text} of the header."""
    import esp_kconfiglib.core as k
    out = {}
    for sym in kconf.unique_defined_syms:
        if sym.choice is not None or INTERNAL.fullmatch(sym.name):
            continue
        v = sym.str_value if k.expr_value(sym.direct_dep) else _off_value(k, sym)
        text = _c_text(k, sym, v)
        if text is not None:
            out[sym.name] = text
    rmii = [out.pop(f"ETH_RMII_{s}_PIN") for s in RMII if f"ETH_RMII_{s}_PIN" in out]
    if out.get("ETH_DRIVER") == "0" and out.get("TRANSPORT_ETH") == "1":
        out["ETH_RMII_PINS"] = ", ".join(rmii)
    out["CMD_TABLE"] = _cmd_table(kconf.syms["CMD_TABLE"].str_value or _off_value(k, kconf.syms["CMD_TABLE"]))
    why = layout.check(_family(kconf), int(kconf.syms["FLASH_KB"].str_value),
                       {name: int(v, 0) for name, v in out.items() if SLOT.fullmatch(name)},
                       int(out["UPGRADE_MODE"]), int(out["IMAGE_NUMBER"]))
    if why and out.get("LAYOUT_NO_UPGRADE") != "1":
        die(why + " - or UMCUB_CFG_LAYOUT_NO_UPGRADE")
    out.update(derived(kconf, out))
    return out


def _family(kconf):
    return next(f for f in ("STM32F1", "STM32F4", "STM32H7") if kconf.syms[f"FAMILY_{f}"].str_value == "y")


def derived(kconf, c):
    """The values that follow from the options ({NAME: C text})."""
    n = lambda name: int(c.get(name, "0"), 0)           # noqa: E731
    family = _family(kconf)
    links = [("TRANSPORT_UART", "UART_LINK"), ("TRANSPORT_USB_CDC", "USB_CDC_LINK"), ("TRANSPORT_CAN", "CAN_LINK"),
             ("TRANSPORT_ETH", "ETH_LINK"), ("TRANSPORT_USER", "USER_LINK")]
    shared = n("SHARED_RAM_ADDR") if family == "STM32H7" else 0x20000000 + int(kconf.syms["RAM_KB"].str_value) * 1024 - 256
    smp_transports = n("TRANSPORT_UART") | n("TRANSPORT_USB_CDC") | n("TRANSPORT_CAN") | n("TRANSPORT_ETH") | n("TRANSPORT_USER")
    d = {"BOOT_INFO_ADDR": n("BOOT_ADDR") + n("BOOT_SIZE") - 256,       # last 256 bytes of the bootloader
         "SHARED_RAM_ADDR": shared,
         # TEST ONLY (power-loss emulation): kept over reset, used by nobody else
         "TEST_FAULT_ADDR": 0x3800F000 if family == "STM32H7" else shared + 240,
         "FAMILY_WRITE_ALIGN": layout.WRITE_ALIGN[family],
         "FAMILY_MIN_SECTOR": min(s for _, s in layout.sectors(family, int(kconf.syms["FLASH_KB"].str_value))),
         "INSPECT": n("INSPECT_VERIFY") | n("INSPECT_HASH") | n("READBACK"),
         "USB": n("TRANSPORT_USB_CDC") | n("TRANSPORT_USB_DFU"),
         "LINK_ANY": sum(n(a) * n(b) for a, b in links),                # non-zero: umcub link on some transport
         "LINK_SECURE_ANY": sum(n(a) * (n(b) // 2) for a, b in links),  # non-zero: SECURE on some transport
         "SMP": smp_transports * n("SMP_ENABLE"),                       # boot_serial, zcbor, inspect group compiled
         "SLOT_WRITER_BOOT": n("TRANSPORT_USB_DFU") + n("LITE_UPLOAD") + n("PROTO_USER")}   # umcub_slot_* in the bootloader
    return {name: (f"0x{v:X}" if name.endswith("_ADDR") else str(v)) for name, v in d.items()}


def _root_path(text):
    p = Path(text).expanduser()
    return p if p.is_absolute() else ROOT / p


def keys(kconf, bdir):
    """{SIGNING|DEVICE|ADMIN: (path, how)} of the keys the configuration uses,
    how = generate / file / dev. Generated keys are made when missing, never
    replaced."""
    val = lambda n: kconf.syms[n].str_value          # noqa: E731
    out = {}
    for name, (gen, dev) in KEYS.items():
        if val(f"{name}_KEY_DEV") == "y":
            out[name] = (ROOT / "tools/keys" / dev, "dev")
        elif val(f"{name}_KEY_FILE") == "y":
            if not val(f"{name}_KEY_PATH"):
                die(f"{PREFIX}{name}_KEY_PATH: no key file given")
            path = _root_path(val(f"{name}_KEY_PATH"))
            if not path.is_file():
                die(f"{PREFIX}{name}_KEY_PATH: {path} not found")
            out[name] = (path, "file")
        elif val(f"{name}_KEY_GENERATE") == "y":
            d = val("KEYS_DIR") or f"keys/{bdir.name if bdir else val('MCU_PART').lower()}"
            out[name] = (_root_path(d) / gen, "generate")
    return out


def _keygen(path):
    """A new EC P-256 private key (PKCS#8 PEM, like imgtool keygen); an existing
    file is kept (also when a parallel build made it first). True when made."""
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    if path.exists():
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    pem = ec.generate_private_key(ec.SECP256R1()).private_bytes(
        serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption())
    tmp = path.with_name(f".{path.name}.{os.getpid()}")
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(pem)
    try:
        os.link(tmp, path)              # fails when the key exists: never replaced
    except FileExistsError:
        return False
    finally:
        tmp.unlink()
    return True


def write_header(vals, path, inputs):
    w = max(len(n) for n in vals) + len(PREFIX) + 1
    lines = ["/* Generated from Kconfig - do not edit: change the configuration with",
             " * menuconfig or in these files and build again:"]
    lines += [f" *   {f}" for f in inputs]
    lines += [" */", "#ifndef UMCUB_CONFIG_H", "#define UMCUB_CONFIG_H", ""]
    lines += [f"#define {PREFIX + n:<{w}} {v}" for n, v in vals.items()]
    lines += ["", "#endif /* UMCUB_CONFIG_H */", ""]
    _write_if_changed(path, "\n".join(lines))


def plain_values(vals):
    """The header's values as numbers and plain strings ({NAME: int | str})."""
    out = {}
    for name, v in vals.items():
        if v.startswith('"'):
            out[name] = json.loads(v)
        else:
            try:
                out[name] = int(v, 0)
            except ValueError:
                out[name] = v
    return out


def write_tool_files(vals, out_dir, bdir, core):
    """umcub_config.json (umcub.config) and umcub_config.cmake (cmake/umcub_config.cmake)."""
    plain = plain_values(vals)
    board = bdir.name if bdir else plain["MCU_PART"].lower()
    _write_if_changed(Path(out_dir) / JSON, json.dumps(
        {"board": board, "board_dir": str(bdir) if bdir else "", "core": core or "", "values": plain}, indent=1) + "\n")
    lines = ["# Generated from Kconfig - do not edit"]
    for name, v in plain.items():
        text = str(v).replace("\\", "\\\\").replace('"', '\\"').replace("$", "\\$").replace(";", "\\;")
        lines.append(f'set({PREFIX}{name} "{text}")')
    _write_if_changed(Path(out_dir) / CMAKE, "\n".join(lines) + "\n")


def _write_if_changed(path, text):
    path = Path(path)
    if not path.is_file() or path.read_text() != text:
        path.write_text(text)


def generate(board, core, fragments, out_dir):
    """.config, umcub_config.h, keys.cmake (the key files, made when they are to
    be generated) and kconfig.deps (the Kconfig files read, for CMake's
    reconfigure) in out_dir. Returns notes for the user (slots not set, new keys)."""
    out = Path(out_dir)
    notes = _unchanged(board, core, fragments, out)
    if notes is not None:
        return notes
    kconf, bdir, inputs = load(board, core, fragments, out)
    kconf.write_config(str(out / ".config"))
    vals = values(kconf)
    write_header(vals, out / HEADER, inputs)
    write_tool_files(vals, out, bdir, core)
    notes = []
    if int(vals["IMG0_PRIMARY_SIZE"], 0) == 0:
        notes.append(f"no slots set yet: choose them in menuconfig (Flash layout) or a defconfig. "
                     f"{vals['MCU_PART'].strip(chr(34))} flash from 0x{layout.FLASH_BASE:08X}: "
                     f"{layout.sector_map(_family(kconf), int(kconf.syms['FLASH_KB'].str_value))}; "
                     f"the bootloader takes {vals['BOOT_SIZE']}")
    cmake = ["# Generated from Kconfig: the keys of the configuration (umcub.kconfig.keys)"]
    for name, (path, how) in keys(kconf, bdir).items():
        if how == "generate" and _keygen(path):
            notes.append(f"new {name.lower()} key {path} - keep it and a backup: the devices are built for it")
        cmake.append(f'set(UMCUB_{"HOST" if name == "ADMIN" else name}_KEY "{path.as_posix()}")')
    _write_if_changed(out / "keys.cmake", "\n".join(cmake) + "\n")
    deps = sorted({str(Path(f).resolve()) for f in kconf.kconfig_filenames} | {str(f) for f in inputs} |
                  {__file__, layout.__file__})
    _write_if_changed(out / "kconfig.deps", "\n".join(deps) + "\n")
    (out / DONE).write_text(json.dumps([n for n in notes if not n.startswith("new ")]))
    return notes


def _unchanged(board, core, fragments, out):
    """The notes of the last run when nothing it read or wrote has changed since
    (reading the Kconfig files takes seconds and CMake configures often), else None."""
    bdir = board_dir(board)
    done = out / DONE
    try:
        when = done.stat().st_mtime
        inputs = ([bdir / "defconfig"] if bdir else []) + [Path(f).resolve() for f in fragments]
        if _state(out).get("stamp") != _stamp(bdir, core, inputs):
            return None
        files = [out / ".config", out / HEADER, out / JSON, out / CMAKE, out / "keys.cmake"] + \
            [Path(f) for f in (out / "kconfig.deps").read_text().splitlines()]
        files += [Path(p) for p in re.findall(r'set\(UMCUB_\w+_KEY "([^"]*)"\)', (out / "keys.cmake").read_text())]
        return json.loads(done.read_text()) if all(f.stat().st_mtime <= when for f in files) else None
    except (OSError, ValueError, TypeError):
        return None


def _reload(out_dir):
    st = _state(out_dir)
    if not st:
        die(f"{out_dir}: no configuration yet (configure the build tree first)")
    return load(st["board"], st["core"], st["fragments"], out_dir)


def menuconfig(out_dir):
    """Interactive editing of the build tree's .config."""
    kconf, bdir, inputs = _reload(out_dir)
    os.environ["KCONFIG_CONFIG"] = str(Path(out_dir) / ".config")
    from esp_menuconfig import menuconfig as run
    if run(kconf):                      # CMake generates again: .config is newer than kconfig.done
        vals = values(kconf)
        write_header(vals, Path(out_dir) / HEADER, inputs)
        write_tool_files(vals, out_dir, bdir, _state(out_dir).get("core"))


def savedefconfig(out_dir, path):
    """The build tree's .config as a minimal defconfig (only what differs from the defaults)."""
    kconf, _, _ = _reload(out_dir)
    kconf.write_min_config(str(path))
