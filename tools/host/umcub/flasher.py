"""flash(): one upload of a signed image with the checks around it, for the tool and GUIs."""
from . import lite
from .errors import DeviceError, UmcubError


def flash(dev, img, protocol="smp", image=0, slot=None, test=False, permanent=False, reset=False, chunk=None,
          progress=None, cancel=None):
    """Upload `img` (an Image) to `dev` with `protocol` "smp" or "lite" (the caller knows the bootloader).

    Before: the board type of a link node against the image. slot None: SMP's image field is the image number
    (its primary slot); "primary" / "secondary" name the slot (UMCUB_CFG_SMP_DIRECT_UPLOAD; lite always).
    After the upload the device checks the image without sending it back (docs/host.md).

    Returns how the image was checked: "verify", "hash", or None when the device cannot check it (lite, no
    inspect requests, or an encrypted image in the primary slot without verify). Raises UmcubError:
    flash.invalid / flash.hash_mismatch when the check fails, before the marks and the reset."""
    if dev.identity:
        img.check_board(dev.identity["board_type"])     # the bootloader would refuse it after the transfer
    if (test or permanent) and slot != "secondary":
        raise UmcubError(code="flash.mark_needs_secondary")
    secondary = 1 if slot == "secondary" else 0
    if protocol == "lite":
        lite.upload(dev, img.data, image, secondary, test, permanent, chunk, progress, cancel)
        if reset:
            lite.reset(dev)
        return None
    from . import smp       # smpclient only when used
    with smp.Smp(dev) as s:
        s.upload(img.data, smp.direct_slot(image, secondary) if slot else image, progress, cancel)
        checked = _check(smp, s, img, image, secondary)
        if test or permanent:
            s.mark(img.hash, permanent)
        if reset:
            s.reset()
    return checked


def _check(smp, s, img, image, slot):
    def ask(request, *args):            # None: the bootloader is built without it (UMCUB_CFG_INSPECT_*)
        try:
            return request(image, slot, *args)
        except DeviceError as e:
            if e.rc != smp.InspectErr.ENOTSUP:
                raise
        return None

    valid = ask(s.verify)
    if valid is not None:
        if not valid:
            raise UmcubError(code="flash.invalid")
        return "verify"
    if img.encrypted and slot == 0:
        return None
    answer = ask(s.hash, 0, img.size)
    if answer is None:
        return None
    if answer[0] != smp.file_hash(img.data):
        raise UmcubError(code="flash.hash_mismatch")
    return "hash"
