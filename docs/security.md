# Encrypted images and security

Images encrypted for one device key, and what the bootloader does and does not protect.

## Encrypted images

`UMCUB_CFG_ENCRYPT_IMAGES` makes MCUboot accept images encrypted for the **device key** (ECIES-P256, AES-128-CTR;
the same `UMCUB_DEVICE_KEY` as the SECURE link). An image file then does not show the firmware, wherever it is
stored or sent. The signature still covers the plain image, so encryption adds confidentiality, not trust.

`umcub_sign_image()` and `tools/host/umcub_image.py sign` then write two files:

| File | Use |
|---|---|
| `<app>.signed.bin` / `.hex` | plain: programmers and debuggers write it straight into the primary slot |
| `<app>.encrypted.bin` | updates: SMP, USB DFU, `umcub_slot_*` from the application; distribution |

(`ENCRYPT_KEY` / `--encrypt-key` select the device key; the default is the development key with a warning.)

How an encrypted image is installed:
- **through the secondary slot** (DFU, application, SMP with `UMCUB_CFG_SMP_DIRECT_UPLOAD`): MCUboot decrypts it
  while it copies or swaps it into the primary slot. In the swap modes the old image is encrypted again on its way
  into the secondary slot if it came encrypted (one written plain by a programmer stays plain), so a revert works and
  the plain code is only ever in the primary slot. Until the swap is over MCUboot keeps the keys it needs in the slot
  trailers: with `UMCUB_CFG_SWAP_SAVE_ENCTLV` (default) the encrypted key TLVs, unwrapped with the device key when an
  interrupted swap resumes - flash never holds a plain image key (H7: +116 bytes, +256 bytes of RAM);
- **SMP upload into the primary slot** (recovery, the default): after the last chunk the bootloader first checks the
  image (signature over the decrypted payload, board type) and answers the last chunk with an error (rc 3) if it is
  not valid for this device - such an image is left as it is and not booted. A valid image is decrypted in place, sector by sector through one sector of RAM (`UMCUB_CFG_ENC_INPLACE`; 128 KiB on the H7). Like any
  upload into the primary slot this is not power-fail safe: an interrupted decryption leaves an invalid image and
  the bootloader in recovery, upload again. The H7 CM4 bootloader has no room for the buffer (`UMCUB_CFG_ENC_INPLACE`
  is 0 there): use the secondary slot.
  With two images (H755 SINGLE_BOOT) the same holds for the primary slot of image 1 (the CM4); encrypted CM4
  updates can also go through its secondary slot: USB DFU alt 1, SMP with `UMCUB_CFG_SMP_DIRECT_UPLOAD` (`image` 4,
  then `image state-write`), the application or the lite upload.

Installed images are plain text in flash, so `read` / SMP read / DFU upload (`UMCUB_CFG_READBACK`) answer only
inside an encrypted umcub link session (`UMCUB_LINK_SECURE` + `UMCUB_CFG_LINK_ENCRYPT`) and refuse everywhere else
(`? readback only in an encrypted session`, SMP rc 11). `verify` checks an encrypted image in the secondary slot by
decrypting it on the fly; `hash` is computed over what is in flash (ciphertext in the secondary slot, plain text in
the primary slot after installation). Outside an encrypted session only whole-image hashes are answered: hashes of
small ranges would reveal the plain code piece by piece. Not available in the direct-xip modes, where images run from both slots.
Protect the device key like the SECURE link: RDP on, see [umcub link](link.md#limits).

## Security

`tools/keys/dev-ecdsa-p256.pem` is a **development key** stored in the repository — anybody can sign images with it.
For a product generate your own (`imgtool keygen -t ecdsa-p256 -k prod.pem`) and pass `-DUMCUB_SIGNING_KEY=prod.pem`
when building the bootloader and the applications. Also write-protect the bootloader sector (WRP) and enable RDP.
For transports on a shared bus see the SECURE mode of the [umcub link](link.md)
and its own keys.

`UMCUB_CFG_SIGNATURE UMCUB_SIGN_NONE` drops the signature (about 4.6 K): images carry a SHA-256 only, so the
bootloader still refuses damaged images and images for another board type, but boots any correctly built image -
there is no authenticity. CMake warns when it is configured; use it only for closed devices on parts where the
ECDSA code does not fit (host test variant `sign_none`, build matrix `bluepill-sign-none`).
