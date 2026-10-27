# umcub link

Several devices on one bus: addressing, the SECURE mode (authentication, encryption) and the host tool.

## Addressing

SMP over a serial line is point to point: on a shared RS485 bus every bootloader would answer at once, and anybody
on the bus could drive them. The umcub link puts an envelope around SMP and text commands, per transport:

| `UMCUB_CFG_<UART\|USB_CDC\|CAN\|ETH\|USER>_LINK` | What the transport accepts |
|---|---|
| `UMCUB_LINK_PLAIN` (default) | SMP and text as before; standard `mcumgr` / `smpmgr` |
| `UMCUB_LINK_ADDRESSED` | only umcub link frames: a device answers frames for its node address (or, unassigned, for its UID after a `HELLO` with that UID), discovery with random back-off |
| `UMCUB_LINK_SECURE` | addressed, and nothing is executed before the host has authenticated; every frame of the session carries a MAC |

Frames: magic, version, type, flags, destination, source, sequence number, length, payload, optional 16-byte tag.
Packet transports (CAN ISO-TP, UDP, board packet transports) carry them as they are, stream transports (UART, USB
CDC) as a line `0x05 0x0B <base64> \n`. The format and the handshake are described in
[`src/proto/include/umcub_link.h`](../src/proto/include/umcub_link.h). RS485 needs the driver-enable pin
(`UMCUB_CFG_UART_DE_PIN`; hardware DE on the H7, software on the F1) and no log on that UART. A 5 V transceiver
(MAX485 and alike) does not drive the bus properly from 3.3 V: power it from 5 V and put its RO output on a
5 V-tolerant RX pin, or use a 3.3 V part (MAX3485, SP3485).

## SECURE mode

The host answers a fresh 32-byte challenge with an ephemeral EC P-256 key, its own nonce and an
ECDSA signature made with the **admin key**. The bootloader checks the signature with the embedded public half,
derives the session keys from ECDH with its **device key** (HKDF-SHA256) and proves in `AUTH_OK` that it holds that
key. After that every frame has an HMAC-SHA256 tag (16 bytes) and a strictly increasing sequence number, so a
recorded frame or `AUTH` cannot be replayed and a frame cannot be changed. `UMCUB_CFG_LINK_ENCRYPT` also encrypts
the payloads (AES-128-CTR), so a sniffer does not see the firmware either. A session belongs to one host on one
transport and ends with `CLOSE` or after `UMCUB_CFG_LINK_SESSION_MS` without a valid frame.

Keys are files, like the signing key:

```sh
imgtool keygen -t ecdsa-p256 -k keys/device.pem     # stays in the bootloader (private)
imgtool keygen -t ecdsa-p256 -k keys/admin.pem      # stays on the hosts allowed to update
cmake ... -DUMCUB_DEVICE_KEY=$PWD/keys/device.pem -DUMCUB_HOST_KEY=$PWD/keys/admin.pem
```

CMake embeds them with `tools/host/umcub_keys.py` (`c`: device private key and admin public key for the bootloader;
`host-c`: the host side for C tools and tests); the device key for [encrypted images](security.md#encrypted-images) with
`imgtool getpriv`. `tools/keys/dev-device-p256.pem` and `dev-admin-p256.pem` are
development keys from the repository, the build warns about them.

### Limits

- The device key is the same in every device of a product. With flash readout protection off it can be read out
  with a debugger, so a SECURE transport stays closed (`HELLO` gets an `ANNOUNCE` flagged closed) while RDP is at
  level 0, unless `UMCUB_CFG_LINK_REQUIRE_RDP` is 0 (development only).
- USB DFU cannot authenticate the host: together with a SECURE transport it is refused at build time unless
  `UMCUB_CFG_USB_DFU_ALLOW_UNAUTH` is set.
- The F1 and the F411 have no true RNG: nonces come from the noise of the low ADC bits (internal temperature
  sensor; measured per 32-byte block at room temperature with `tests/hw/entropy_test.py`: 60-80 bits of
  min-entropy on a Blue Pill, about 99 bits on a Black Pill F411),
  hashed with the UID, a boot counter kept in RAM over resets and (SECURE) keyed with the device key. That is
  plenty for challenges that must not repeat; a colder or quieter part has less noise, so measure a new board. Randomness is fail-closed: if the entropy source fails
  or its output looks stuck, `HELLO` gets no challenge.
- A failed `AUTH` blocks further attempts for one second, a challenge is valid for ten seconds. An `AUTH` costs two
  ECC operations (about 1.3 s on a 72 MHz Cortex-M3), so with SECURE `UMCUB_CFG_WATCHDOG_MS` must be at least 2000
  and `UMCUB_CFG_VALIDATE_PRIMARY` must stay on (both checked at build time).
- The policy is per transport: a PLAIN transport next to a SECURE one is a way around it, the build warns unless
  `UMCUB_CFG_LINK_MIXED_OK` is set. A UART with a link never carries the log.
- A bus can always be jammed; the link protects authenticity, integrity, replay and (with encryption)
  confidentiality.
- Firmware confidentiality needs two things: `UMCUB_CFG_LINK_ENCRYPT` for the session and
  [encrypted images](security.md#encrypted-images) for what is stored and sent outside it.

## Host side: the `umcub` tool

Standard SMP clients do not speak the link. The [`umcub` tool](host.md) finds the devices, selects one, uploads to
it and, as a proxy, serves `mcumgr` / `smpmgr` unchanged.

`discover` broadcasts DISCOVER with a number of slots; every device answers ANNOUNCE in a random slot. Two devices
in one slot collide and the host receives garbage; the next round lists the UIDs found so far (those stay silent)
and the rest draw new slots. Rounds end after a quiet one - nothing new and nothing garbled - or after `--rounds`
(default 8). Fewer `--slots` make discovery faster and collisions more likely.

```sh
umcub -d /dev/ttyUSB0 discover                                # all devices on the bus (random back-off rounds)
umcub -d /dev/ttyUSB0 --node 5 info                           # identity; SECURE: authenticates first
umcub -d /dev/ttyUSB0 --uid 53ff7206... cmd i                 # unassigned node, selected by UID
umcub -d /dev/ttyUSB0 --node 5 --admin-key admin.pem --device-key device.pem upload app.encrypted.bin
umcub -d /dev/ttyUSB0 --node 5 --admin-key admin.pem --device-key device.pem serve --pty-link /tmp/smp
smpmgr --port /tmp/smp --line-buffers 4 image upload app.encrypted.bin    # through the proxy
umcub -d 192.168.1.50 --node 5 serve --udp-listen 127.0.0.1:1337   # mcumgr --conntype udp
umcub -d /dev/ttyUSB1 sniff                                   # listen only: every frame on the bus, decoded
```

SECURE transports need `--admin-key` (private) and `--device-key` (the public half is enough). Your own host
software can do the same: the frames and the handshake are in `umcub_link.h`, `tools/host/umcub/link.py` is a complete
reference (Python `cryptography`).
