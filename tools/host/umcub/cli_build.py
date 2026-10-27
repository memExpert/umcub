"""Build-side commands of the umcub tool (see umcub.cli): image info / sign, keys.
They need no device and none of the transport libraries."""
import sys

from . import keys
from .config import MODES, ROOT, Config
from .sign import DEV_DEVICE_KEY, DEV_KEY, sign


def _log(text):
    print(text, file=sys.stderr if text.startswith("warning") else sys.stdout)


def cmd_sign(cfg, a):
    sign(cfg, a.input, a.image, a.slot, a.version, a.key, a.encrypt_key, a.confirm, a.pad, a.depends, a.output,
         a.imgtool, log=(lambda t: None if a.quiet and t.startswith("warning") else _log(t)))
    return 0


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
            print(f"  sign      umcub image sign --board {a.board}{' --core ' + core if core else ''} "
                  f"--image {img}{' --slot ' + str(slot) if cfg.xip() else ''} --version <x.y.z> <app.elf|.axf|.hex>")
    shared = cfg["SHARED_RAM_ADDR"]
    print(f"\nkeep free: 0x{shared:08X}..0x{shared + 256:08X} (bootloader handoff, kept over reset)")
    if cfg["DUALCORE_MODE"]:
        print("           dual-core: keep the handoff areas of both cores free (H7: 0x3800FE00..0x38010000)")
    print("VTOR:      set by the bootloader - leave USER_VECT_TAB_ADDRESS undefined in system_stm32*.c")
    return 0


def add_image_arguments(sub):
    for name, what in (("info", "where to link the application, what RAM to keep free, IDE settings"),
                       ("sign", "sign an ELF/AXF, Intel HEX or raw .bin -> <name>.signed.bin + .signed.hex")):
        p = sub.add_parser(name, help=what)
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
            p.add_argument("--quiet", action="store_true",
                           help="no notes about development keys (CMake warns at configure time)")


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
        cfg = Config(a.board, a.core, a.pre, a.post, a.cc)
        return (cmd_sign if a.sub == "sign" else cmd_info)(cfg, a)
    if a.sub == "pub":
        print(keys.raw_pub(keys.load(a.key)).hex())
    else:
        keys.write_if_changed(a.output, (keys.device_c if a.sub == "c" else keys.host_c)(a.device, a.admin))
    return 0
