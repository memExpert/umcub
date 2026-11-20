# Encrypted images and security

Images encrypted for one device key, and what the bootloader does and does not protect.

## Encrypted images

`UMCUB_CFG_ENCRYPT_IMAGES` makes MCUboot accept images encrypted for the **device key** (ECIES-P256, AES-128-CTR;
the same device key as the SECURE link). An image file then does not show the firmware, wherever it is
stored or sent. The signature still covers the plain image, so encryption adds confidentiality, not trust. Not
available in the direct-xip modes, where images run from both slots.

`umcub_sign_image()` and `umcub image sign` then write two files:

| File | Use |
|---|---|
| `<app>.signed.bin` / `.hex` | plain: programmers and debuggers write it straight into the primary slot |
| `<app>.encrypted.bin` | updates: SMP, USB DFU, `umcub_slot_*` from the application; distribution |

(`ENCRYPT_KEY` / `--encrypt-key` select the device key; the default is the configuration's, menu *Keys*.)

How an encrypted image is installed:
- **through the secondary slot** (DFU, application, SMP with `UMCUB_CFG_SMP_DIRECT_UPLOAD`): MCUboot decrypts it
  while it copies or swaps it into the primary slot. In the swap modes the old image is encrypted again on its way
  into the secondary slot if it came encrypted (one written plain by a programmer stays plain), so a revert works and
  the plain code is only ever in the primary slot. Until the swap is over MCUboot keeps the keys it needs in the slot
  trailers: with `UMCUB_CFG_SWAP_SAVE_ENCTLV` (default) the encrypted key TLVs, unwrapped with the device key when an
  interrupted swap resumes - flash never holds a plain image key (H7: +116 bytes, +256 bytes of RAM);
- **SMP upload into the primary slot** (recovery, the default): after the last chunk the bootloader first checks the
  image (signature over the decrypted payload, board type) and answers the last chunk with an error (rc 3) if it is
  not valid for this device - such an image is left as it is and not booted. A valid image is decrypted in place,
  sector by sector through one sector of RAM (`UMCUB_CFG_ENC_INPLACE`; 128 KiB on the H7), after that answer (a
  large image takes longer than SMP clients wait for one): a flash error while decrypting is not in the answer.
  Check the slot afterwards - `umcub upload` asks the device to validate it, with other clients run an image list
  or `umcub verify`. Like any upload into the primary slot this is not power-fail safe: an interrupted decryption
  leaves an invalid image and the bootloader in recovery, upload again. The H7 CM4 bootloader has no room for the
  buffer (`UMCUB_CFG_ENC_INPLACE` is 0 there, also on the F4): use the secondary slot - an encrypted upload into the
  primary slot is answered rc 3.
  With two images (H755 SINGLE_BOOT) the same holds for the primary slot of image 1 (the CM4); encrypted CM4
  updates can also go through its secondary slot: USB DFU alt 1, SMP with `UMCUB_CFG_SMP_DIRECT_UPLOAD` (`image` 4,
  then `image state-write`), the application or the lite upload.

Installed images are plain text in flash, so `read` and SMP read (`UMCUB_CFG_READBACK`) answer only inside an
encrypted umcub link session (`UMCUB_CFG_<transport>_LINK_SECURE` + `UMCUB_CFG_LINK_ENCRYPT`; DFU upload is not built
with encrypted images) and refuse everywhere else
(`? readback only in an encrypted session`, SMP rc 11). `verify` checks an encrypted image in the secondary slot by
decrypting it on the fly; `hash` is computed over what is in flash (ciphertext in the secondary slot, plain text in
the primary slot after installation). Hashes of slot ranges need `UMCUB_CFG_READBACK` like `read` (hashes of small
ranges reveal the code piece by piece; without it SMP answers rc 8, not supported); with encrypted images they too are
answered only inside an encrypted session.
Protect the device key like the SECURE link: RDP on, see [umcub link](link.md#limits).

## Security

`tools/keys/dev-ecdsa-p256.pem` is a **development key** stored in the repository — anybody can sign images with it.
Builds use it only when the configuration asks for it (`UMCUB_CFG_SIGNING_KEY_DEV`): by default the first configure
generates a key in `keys/<board>/`, or takes your own file, see [Kconfig](configuration.md#kconfig). Also write-protect the bootloader sector (WRP) and enable RDP.
For transports on a shared bus see the SECURE mode of the [umcub link](link.md)
and its own keys. With encrypted images or a SECURE link the bootloader embeds the device private key: the generated
`umcub_enc_key.c` / `umcub_link_keys.c` are written readable by their owner only, and the bootloader ELF / HEX / BIN of
such a build are secrets like the key file itself (do not publish build trees or attach them to tickets).

On a PLAIN or ADDRESSED transport the SMP parser sees requests before any authentication. umcub builds MCUboot's
serial recovery with zcbor 0.8.3 (`third_party/zcbor`, not the 0.8.1 copy inside MCUboot): it skips unknown values
at most 8 lists / maps deep (`ZCBOR_MAX_SKIP_DEPTH`), so a small, deeply nested request cannot exhaust the stack.

tinyUSB 0.21.0 answers some standard and DFU GET requests from a stack variable: a host that sends one with the
wrong direction (OUT) makes the stack write land after the function returned. Until a tinyUSB release fixes it, keep
USB recovery (`UMCUB_CFG_TRANSPORT_USB_CDC` / `_USB_DFU`) off where untrusted hosts can reach the USB port.

`UMCUB_CFG_SIGNATURE_NONE` drops the signature (about 4.6 K): images carry a SHA-256 only, so the
bootloader still refuses damaged images and images for another board type, but boots any correctly built image -
there is no authenticity. CMake warns when it is configured; use it only for closed devices on parts where the
ECDSA code does not fit (host test variant `sign_none`, build matrix `bluepill-sign-none`).
