"""The umcub configuration from Kconfig (esp-idf-kconfig): a board's defconfig
plus configuration fragments -> .config and the C header umcub_config.h.

Symbols are written as UMCUB_CFG_<NAME> in both (.config lines, #defines).
In the header: bool -> 0 / 1, int and hex as numbers, pins ("PA9", "PD12:7"
with the alternate function) -> UMCUB_PIN(), IPv4 addresses -> UMCUB_IP4(),
the MCU as the CMSIS device define, other strings as C strings. Choice members
are left out (a hidden int carries the choice), as are symbols whose
dependencies are not met.

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
IP_NAMES = {"ETH_IP", "ETH_NETMASK"}
RAW_NAMES = {"MCU"}             # C tokens, not strings
# Kconfig helpers that stay out of the header: what the part has, pin sets,
# lines and families (port/kconfig/*.kconfig, port/Kconfig, Kconfig).
INTERNAL = re.compile(r"(PIN|PINSET|HAS|FAMILY)_\w+|MCU_STM32\w+|BUILD_CORE|CORE_CM4|MCU_DUALCORE|LINK_SECURE"
                      r"|KEYS_DIR|(SIGNING|DEVICE|ADMIN)_KEY_\w+")
# The keys (config/Kconfig "Keys"): generated file name, development key of the repository.
KEYS = {"SIGNING": ("signing.pem", "dev-ecdsa-p256.pem"),
        "DEVICE": ("device.pem", "dev-device-p256.pem"),
        "ADMIN": ("admin.pem", "dev-admin-p256.pem")}
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


def _kconfig(bdir, core):
    os.environ.update({"CONFIG_": PREFIX, "UMCUB_ROOT": ROOT.as_posix(),
                       "UMCUB_BOARD_DIR": Path(bdir).as_posix() if bdir else (ROOT / "boards/.none").as_posix(),
                       "UMCUB_CORE": core or ""})
    try:
        import esp_kconfiglib.core as k
    except ImportError:
        die("esp-idf-kconfig is not installed (pip install esp-idf-kconfig; tools/setup.sh does it)")
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
    A later assignment overrides an earlier one, of the symbol or of its choice."""
    last, errs = {}, []
    for f in inputs:
        for n, name, v in _assignments(f):
            sym = kconf.syms.get(name)
            if sym is None or not sym.nodes:
                errs.append(f"{f}:{n}: {PREFIX}{name}: no such option")
                continue
            key = sym.choice if sym.choice is not None and v == "y" else sym
            last[key] = (f, n, sym, v)
    for f, n, sym, v in last.values():
        got = sym.str_value
        if got != v and not (v.lower().startswith("0x") and got.lower() == v.lower()):
            dep = " (depends on " + kconf.expr_str(sym.direct_dep) + ")" if got in ("", "n") else ""
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
        _load_dot(kconf, dot)
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


def _load_dot(kconf, dot):
    """A full .config, without what does not need loading: values marked
    "# default:" and the unselected members of choices. esp-kconfiglib takes
    minutes for the full file (hundreds of part and pin choice members), seconds
    for the rest - same result."""
    keep, default = [], False
    for line in Path(dot).read_text().splitlines():
        if line.strip() == "# default:":
            default = True
            continue
        m = re.fullmatch(rf"# {PREFIX}(\w+) is not set", line.strip())
        if not default and not (m and m.group(1) in kconf.syms and kconf.syms[m.group(1)].choice is not None):
            keep.append(line)
        default = False
    tmp = Path(dot).with_name(".config.load")
    tmp.write_text("\n".join(keep) + "\n")
    try:
        kconf.load_config(str(tmp))
    finally:
        tmp.unlink()


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


def values(kconf):
    """{NAME: C text} of the symbols that go into the header."""
    import esp_kconfiglib.core as k
    out = {}
    for sym in kconf.unique_defined_syms:
        if sym.choice is not None or INTERNAL.fullmatch(sym.name):
            continue
        v = sym.str_value
        if k.expr_value(sym.direct_dep) == 0:
            continue                    # dependencies not met: no value
        if sym.type == k.BOOL:
            out[sym.name] = "1" if v == "y" else "0"
        elif sym.type in (k.INT, k.HEX):
            if v != "":
                out[sym.name] = v
        elif sym.type == k.STRING:
            if sym.name in RAW_NAMES:
                if v:
                    out[sym.name] = v
            elif sym.name.endswith("_PINS"):
                out[sym.name] = ", ".join(_pin(sym.name, p) for p in v.split())
            elif sym.name.endswith("_PIN"):
                out[sym.name] = _pin(sym.name, v)
            elif sym.name in IP_NAMES:
                out[sym.name] = _ip(sym.name, v)
            else:
                out[sym.name] = json.dumps(v)
    rmii = [out.pop(f"ETH_RMII_{s}_PIN") for s in RMII if f"ETH_RMII_{s}_PIN" in out]
    if rmii:
        out["ETH_RMII_PINS"] = ", ".join(rmii)
    if out.get("LAYOUT_AUTO") == "1":
        out.update({n: f"0x{v:X}" for n, v in auto_layout(kconf).items()})
    return out


def auto_layout(kconf):
    """UMCUB_CFG_LAYOUT_AUTO: the slots from the part's flash and the mode (umcub.layout)."""
    val = lambda n: kconf.syms[n].str_value          # noqa: E731
    family = next(f for f in ("STM32F1", "STM32F4", "STM32H7") if val(f"FAMILY_{f}") == "y")
    return layout.auto(family, int(val("FLASH_KB")), int(val("BOOT_ADDR"), 16), int(val("BOOT_SIZE"), 16),
                       int(val("UPGRADE_MODE")), int(val("IMAGE_NUMBER")))


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


def write_header(kconf, path, inputs):
    vals = values(kconf)
    w = max(len(n) for n in vals) + len(PREFIX) + 1
    lines = ["/* Generated from Kconfig - do not edit: change the configuration with",
             " * menuconfig or in these files and build again:"]
    lines += [f" *   {f}" for f in inputs]
    lines += [" */", "#ifndef UMCUB_CONFIG_H", "#define UMCUB_CONFIG_H", ""]
    lines += [f"#define {PREFIX + n:<{w}} {v}" for n, v in vals.items()]
    lines += ["", "#endif /* UMCUB_CONFIG_H */", ""]
    _write_if_changed(path, "\n".join(lines))


def _write_if_changed(path, text):
    path = Path(path)
    if not path.is_file() or path.read_text() != text:
        path.write_text(text)


def generate(board, core, fragments, out_dir):
    """.config, umcub_config.h, keys.cmake (the key files, made when they are to
    be generated) and kconfig.deps (the Kconfig files read, for CMake's
    reconfigure) in out_dir. Returns notes for the user (layout, new keys)."""
    out = Path(out_dir)
    kconf, bdir, inputs = load(board, core, fragments, out)
    kconf.write_config(str(out / ".config"))
    write_header(kconf, out / HEADER, inputs)
    notes = []
    if kconf.syms["LAYOUT_AUTO"].str_value == "y":
        notes.append("automatic layout: " + "; ".join(layout.describe(auto_layout(kconf))))
    cmake = ["# Generated from Kconfig: the keys of the configuration (umcub.kconfig.keys)"]
    for name, (path, how) in keys(kconf, bdir).items():
        if how == "generate" and _keygen(path):
            notes.append(f"new {name.lower()} key {path} - keep it (and a backup): the devices accept only its images")
        cmake.append(f'set(UMCUB_{"HOST" if name == "ADMIN" else name}_KEY "{path.as_posix()}")')
    _write_if_changed(out / "keys.cmake", "\n".join(cmake) + "\n")
    deps = sorted({str(Path(f).resolve()) for f in kconf.kconfig_filenames} | {str(f) for f in inputs})
    _write_if_changed(out / "kconfig.deps", "\n".join(deps) + "\n")
    return notes


def _reload(out_dir):
    st = _state(out_dir)
    if not st:
        die(f"{out_dir}: no configuration yet (configure the build tree first)")
    return load(st["board"], st["core"], st["fragments"], out_dir)


def menuconfig(out_dir):
    """Interactive editing of the build tree's .config."""
    kconf, _, inputs = _reload(out_dir)
    os.environ["KCONFIG_CONFIG"] = str(Path(out_dir) / ".config")
    from esp_menuconfig import menuconfig as run
    if run(kconf):
        _load_dot(kconf, Path(out_dir) / ".config")
        write_header(kconf, Path(out_dir) / HEADER, inputs)


def savedefconfig(out_dir, path):
    """The build tree's .config as a minimal defconfig (only what differs from the defaults)."""
    kconf, _, _ = _reload(out_dir)
    kconf.write_min_config(str(path))
