"""Signing application images for umcub (what umcub_sign_image() in CMake and IDE
post-build steps run): ELF/AXF, Intel HEX or raw .bin in, `<name>.signed.bin/.hex`
(+ `.encrypted.bin`, `.confirmed.*`) out, with the slot, alignment, trailer and
board type TLV of the board's configuration. imgtool is imported from
third_party/mcuboot (or given as an executable).
"""
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from .config import MODES, ROOT, die

DEV_KEY = ROOT / "tools/keys/dev-ecdsa-p256.pem"
DEV_DEVICE_KEY = ROOT / "tools/keys/dev-device-p256.pem"



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
        return read_hex(path, data)
    if ext == ".bin":
        return [(None, data)]
    die(f"{path}: unknown input format (ELF/AXF, .hex or .bin)")


def flatten(segs, origin, length, path, log=None):
    if segs[0][0] is None:                          # raw .bin: assumed linked at origin
        segs = [(origin, segs[0][1])]
    lo = min(a for a, _ in segs)
    hi = max(a + len(d) for a, d in segs)
    for a, d in segs:
        if a < origin or a + len(d) > origin + length:
            die(f"{path}: data at 0x{a:08X}..0x{a + len(d):08X} is outside the slot window "
                f"0x{origin:08X}..0x{origin + length:08X} - is the flash origin/length in the linker "
                f"settings right? (umcub image info)")
    if lo != origin:
        die(f"{path}: image starts at 0x{lo:08X}, the vector table must be at 0x{origin:08X}")
    img = bytearray(hi - lo)                        # gaps 0x00, like objcopy -O binary
    for a, d in segs:
        img[a - lo:a - lo + len(d)] = d
    if len(img) >= 8:
        sp, reset = struct.unpack_from("<II", img, 0)
        if not (origin <= (reset & ~1) < origin + len(img)) or not reset & 1:
            if log:
                log(f"warning: reset vector 0x{reset:08X} is not a Thumb address inside the image")
    return bytes(img)


def read_hex(path, data):
    """Intel HEX -> [(address, bytes)] of contiguous runs (records 00, 01, 02, 04)."""
    mem, base = {}, 0
    for n, line in enumerate(data.decode(errors="replace").splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        try:
            rec = bytes.fromhex(line[1:]) if line[0] == ":" else b""
        except ValueError:
            rec = b""
        if len(rec) < 5 or len(rec) != rec[0] + 5 or sum(rec) & 0xFF:
            die(f"{path}:{n}: not an Intel HEX record")
        kind, payload, off = rec[3], rec[4:-1], rec[1] << 8 | rec[2]
        if kind == 0:
            for i, b in enumerate(payload):
                mem[base + off + i] = b
        elif kind == 1:
            break
        elif kind in (2, 4):
            base = int.from_bytes(payload, "big") << (4 if kind == 2 else 16)
    segs = []
    for a in sorted(mem):
        if segs and a == segs[-1][0] + len(segs[-1][1]):
            segs[-1][1].append(mem[a])
        else:
            segs.append((a, bytearray([mem[a]])))
    if not segs:
        die(f"{path}: no data records")
    return [(a, bytes(d)) for a, d in segs]


def write_hex(path, data, addr):
    """Intel HEX with 16-byte data records and extended linear addresses."""
    def record(kind, off, payload):
        rec = bytes([len(payload), off >> 8 & 0xFF, off & 0xFF, kind]) + payload
        return ":" + (rec + bytes([-sum(rec) & 0xFF])).hex().upper() + "\n"
    out, high = [], None
    for i in range(0, len(data), 16):
        a = addr + i
        if a >> 16 != high:
            high = a >> 16
            out.append(record(4, 0, high.to_bytes(2, "big")))
        out.append(record(0, a & 0xFFFF, data[i:i + 16]))
    out.append(record(1, 0, b""))
    Path(path).write_text("".join(out))


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


def sign(cfg, input, image=0, slot=0, version="0.0.0", key=None, encrypt_key=None, confirm=False, pad=False,
         depends=None, output=None, imgtool=None, log=None):
    """Sign `input` for image `image` (direct-xip: linked for `slot`) of the board
    configuration `cfg` (umcub.config.Config). Writes <output>.bin (the flat
    image), .signed.bin/.hex, in direct-xip-revert .confirmed.bin/.hex, with
    UMCUB_CFG_ENCRYPT_IMAGES .encrypted.bin; returns the written files.
    log(text) gets notes and warnings."""
    log = log or (lambda text: None)
    mode = cfg["UPGRADE_MODE"]
    slot = slot if cfg.xip() else 0                 # swap/overwrite: always linked for the primary
    slot_addr, slot_size = cfg.slot(image, slot)
    if not slot_size:
        die(f"image {image} slot {slot} is not configured")
    if not cfg.xip():                               # slots may differ in size (swap-offset)
        sec = cfg.slot(image, 1)[1]
        if sec and sec < slot_size:
            slot_size = sec
    origin, length = cfg.window(image, slot)
    img = flatten(load_segments(input), origin, length, input, log)

    align = cfg["FAMILY_WRITE_ALIGN"]
    args = ["sign", "--version", version, "--header-size", str(cfg["IMAGE_HEADER_SIZE"]), "--pad-header",
            "--slot-size", str(slot_size), "--align", str(align), "--max-align", str(align)]
    if cfg["SIGNATURE"] == 1:
        key = Path(key) if key else DEV_KEY
        if key.resolve() == DEV_KEY.resolve():
            log("warning: signing with the development key from the repository")
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
    if confirm:
        args.append("--confirm")
    if pad:
        args.append("--pad")
    if depends:
        args += ["--dependencies", depends]

    base = Path(output) if output else Path(input).with_suffix("")
    unsigned = base.with_name(base.name + ".bin")       # the flat image, like objcopy -O binary
    written = []
    if unsigned.resolve() != Path(input).resolve():
        unsigned.write_bytes(img)
        written.append(unsigned)
    with tempfile.TemporaryDirectory() as d:
        raw = Path(d) / "app.bin"
        raw.write_bytes(img)
        outs = [(args, base.with_name(base.name + ".signed"))]
        if mode == 6 and not confirm:
            outs.append((args + ["--confirm", "--pad"], base.with_name(base.name + ".confirmed")))
        for sargs, out in outs:
            run_imgtool(sargs + [str(raw), str(out) + ".bin"], imgtool)
            write_hex(str(out) + ".hex", Path(str(out) + ".bin").read_bytes(), slot_addr)
            written += [Path(str(out) + ".bin"), Path(str(out) + ".hex")]
            log(f"{out}.bin / .hex: image {image} slot {slot} version {version} at 0x{slot_addr:08X} "
                f"({len(img)} bytes of code, {MODES.get(mode, mode)})")
        if cfg["ENCRYPT_IMAGES"]:                   # for updates; the plain .signed.* is for programmers
            dkey = Path(encrypt_key) if encrypt_key else DEV_DEVICE_KEY
            if dkey.resolve() == DEV_DEVICE_KEY.resolve():
                log("warning: encrypting with the development device key from the repository")
            out = base.with_name(base.name + ".encrypted.bin")
            eargs = ["--encrypt", str(dkey)]
            if cfg["SWAP_SAVE_ENCTLV"] and 2 <= mode <= 4:  # trailer holds the key TLVs
                eargs.append("--save-enctlv")
            run_imgtool(args + eargs + [str(raw), str(out)], imgtool)
            written.append(out)
            log(f"{out}: the same image encrypted with {dkey.name} (SMP / DFU / application updates)")
    return written
