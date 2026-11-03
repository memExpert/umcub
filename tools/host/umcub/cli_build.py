"""Build-side commands of the umcub tool (see umcub.cli): image info / sign, keys.
They need no device and none of the transport libraries."""
import sys

from . import kconfig, keys, linker
from .config import MODES, ROOT, Config
from .sign import DEV_DEVICE_KEY, DEV_KEY, boot_usage, sign


def _log(text):
    print(text, file=sys.stderr if text.startswith("warning") else sys.stdout)


def cmd_sign(cfg, a):
    sign(cfg, a.input, a.image, a.slot, a.version, a.key, a.encrypt_key, a.confirm, a.pad, a.depends, a.output,
         a.imgtool, log=(lambda t: None if a.quiet and t.startswith("warning") else _log(t)))
    return 0


def cmd_ld(cfg, a):
    for path in linker.write(cfg, a.out, a.core or ""):
        print(path)
    return 0


def cmd_info(cfg, a):
    mode = cfg["UPGRADE_MODE"]
    print(f"{cfg.board_name}{' / ' + a.core if a.core else ''}: {cfg.mcu}, {MODES.get(mode, mode)}, "
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
            print(f"  sign      umcub image sign --board {a.board}{' --core ' + core if core else ''} "
                  f"--image {img}{' --slot ' + str(slot) if cfg.xip() else ''} --version <x.y.z> <app.elf|.axf|.hex>")
    shared = cfg["SHARED_RAM_ADDR"]
    print(f"\nkeep free: 0x{shared:08X}..0x{shared + 256:08X} (bootloader handoff, kept over reset)")
    if cfg["DUALCORE_MODE"]:
        print("           dual-core: keep the handoff areas of both cores free (H7: 0x3800FE00..0x38010000)")
    print("VTOR:      set by the bootloader - leave USER_VECT_TAB_ADDRESS undefined in system_stm32*.c")
    return 0


def cmd_size(cfg, a):
    u = boot_usage(cfg, a.input)
    print(f"umcub: bootloader {u['used']} B, {u['free']} B free (region 0x{cfg['BOOT_SIZE']:X}, its last "
          f"{cfg['BOOT_ADDR'] + cfg['BOOT_SIZE'] - cfg['BOOT_INFO_ADDR']} B: info block)"
          + (f"; UMCUB_CFG_BOOT_SIZE=0x{u['enough']:X} would be enough" if u["enough"] else ""))
    for img in cfg.images():
        for slot in ((0, 1) if cfg.xip() else (0,)):
            origin, length = cfg.window(img, slot)
            print(f"umcub: application image {img}{' slot ' + str(slot) if cfg.xip() else ''}: up to {length} B "
                  f"at 0x{origin:08X} (slot 0x{cfg.slot(img, slot)[1]:X} less header "
                  f"0x{cfg['IMAGE_HEADER_SIZE']:X}, trailer 0x{cfg['TRAILER_RESERVE']:X})")
    return 0


def add_image_arguments(sub):
    for name, what in (("info", "where to link the application, what RAM to keep free, IDE settings"),
                       ("ld", "GNU ld MEMORY blocks of the slots (INCLUDE umcub_app_img<n>_slot<m>.ld)"),
                       ("sign", "sign an ELF/AXF, Intel HEX or raw .bin -> <name>.signed.bin + .signed.hex"),
                       ("size", "what a bootloader takes of its region, the room for the applications")):
        p = sub.add_parser(name, help=what)
        p.add_argument("--board", required=True, help="board name under boards/, a directory with a defconfig, "
                       "or a build tree's umcub_kconfig directory; \"\": no board (--conf)")
        p.add_argument("--core", choices=("cm7", "cm4"), help="dual-core parts: core the application runs on")
        p.add_argument("--conf", action="append", help="configuration fragment (repeatable, as for the build)")
        if name == "ld":
            p.add_argument("-o", "--out", default=".", help="output directory (default: current)")
        if name == "size":
            p.add_argument("input", help="the bootloader's ELF or Intel HEX")
        if name == "sign":
            p.add_argument("input", help="ELF/AXF, Intel HEX or raw .bin linked for the slot")
            p.add_argument("--image", type=int, default=0)
            p.add_argument("--slot", type=int, default=0, help="direct-xip: slot the image is linked for")
            p.add_argument("--version", default="0.0.0", help="x.y.z[+build]")
            p.add_argument("--key", help="signing key (default: the configuration's)")
            p.add_argument("--encrypt-key", help="UMCUB_CFG_ENCRYPT_IMAGES: device key for <out>.encrypted.bin "
                                                  f"(default {DEV_DEVICE_KEY.relative_to(ROOT)})")
            p.add_argument("--confirm", action="store_true", help="mark the image confirmed (image_ok)")
            p.add_argument("--pad", action="store_true", help="pad to the slot size with a trailer")
            p.add_argument("--depends", help='multi-image dependency, e.g. "(1,1.0.0)"')
            p.add_argument("-o", "--output", help="output base name (default: input without extension)")
            p.add_argument("--imgtool", help="imgtool executable instead of the bundled module")
            p.add_argument("--quiet", action="store_true",
                           help="no notes about development keys (CMake warns at configure time)")


def cmd_config(a):
    if a.sub == "generate":
        for note in kconfig.generate(a.board, a.core, a.conf or (), a.out):
            print(f"umcub: {note}", file=sys.stderr if note.startswith("new ") else sys.stdout)    # CMake: a warning
        linker.write(Config(a.out, a.core), a.out, a.core)
    elif a.sub == "menu":
        kconfig.menuconfig(a.out)
    else:
        kconfig.savedefconfig(a.out, a.output)
        print(f"{a.output}: minimal configuration (copy it to the board's defconfig or a fragment)")
    return 0


def add_config_arguments(sub):
    p = sub.add_parser("generate", help=".config and umcub_config.h of a Kconfig board (CMake runs it)")
    p.add_argument("--board", required=True, help="board name under boards/ or a directory with a defconfig")
    p.add_argument("--core", default="", help="dual-core parts: cm7 or cm4")
    p.add_argument("--conf", action="append", help="configuration fragment (repeatable, applied in order)")
    p.add_argument("--out", required=True, help="output directory (the build tree's umcub_kconfig)")
    p = sub.add_parser("menu", help="menuconfig on a build tree's configuration")
    p.add_argument("--out", required=True, help="the build tree's umcub_kconfig directory")
    p = sub.add_parser("save", help="a build tree's configuration as a minimal defconfig")
    p.add_argument("--out", required=True, help="the build tree's umcub_kconfig directory")
    p.add_argument("-o", "--output", required=True, help="file to write")


def add_keys_arguments(sub):
    for name, what in (("c", "bootloader side: device private key, admin public key"),
                       ("host-c", "host side for C tools and tests: admin private key, device public key")):
        p = sub.add_parser(name, help=what)
        p.add_argument("--device", required=True)
        p.add_argument("--admin", required=True)
        p.add_argument("-o", "--output", required=True)
    p = sub.add_parser("pub", help="public key, hex (X || Y)")
    p.add_argument("key")


def run(a):
    if a.cmd == "image":
        cfg = Config(a.board, a.core, a.conf or ())
        for note in cfg.notes:
            if note.startswith("new "):
                _log(f"umcub: {note}")
        return {"sign": cmd_sign, "ld": cmd_ld, "info": cmd_info, "size": cmd_size}[a.sub](cfg, a)
    if a.cmd == "config":
        return cmd_config(a)
    if a.sub == "pub":
        print(keys.raw_pub(keys.load(a.key)).hex())
    else:
        keys.write_if_changed(a.output, (keys.device_c if a.sub == "c" else keys.host_c)(a.device, a.admin))
    return 0
