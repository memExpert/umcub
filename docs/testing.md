# Testing

The build matrix, host tests, simulated devices and the hardware tests.

```sh
tools/build/build_matrix.sh                 # 33 bootloader configurations (side by side, JOBS=n), examples, IDE checks,
                                            # host tests; warning-free
ctest --test-dir build/matrix/host    # host tests only
tests/host/link_e2e.py build/matrix/host   # simulated devices: umcub link bus, smpmgr, host tools, lite upload
tests/tools/check_pins_test.py              # pin check: right / wrong pins and AF numbers, packages
tools/build/check_docs.py                   # README and docs/ still match the repository (size tables, links, ...)
```

Host tests (`tests/host`) run the real MCUboot (boot_go, boot_serial, ECDSA) on an emulated H7 flash (32-byte
words, no double programming) through the umcub transport layer. Covered: SMP upload / echo / list over the packet
and stream paths (including two interleaved streams), swap → revert → confirm in scratch / move / offset modes, SMP
upload into the secondary slot (`UMCUB_CFG_SMP_DIRECT_UPLOAD`) marked by `image state-write` for a test or for good, text
commands, verify / hash / read, ISO-TP classic/FD, DHCP / ARP / ICMP, board-supplied drivers and transports, the
status LED patterns (listening, data, a failed transport init), the
board-type check, the umcub link in ADDRESSED mode and in SECURE mode with encryption (handshake, wrong admin key,
replayed / tampered / reordered frames, session end, RDP level 0), encrypted images (SMP upload into the primary
slot decrypted in place, swap upgrade / revert / confirm with re-encryption, verify of an encrypted secondary,
readback only inside an encrypted session). `link_e2e.py` runs device simulators (the real mux, link, boot_serial and
CAN transport on emulated flash) against the host tools: `umcub_link.py` with three nodes on one bus,
`umcub_inspect.py` on plain SMP, `smp_can.py` on the CAN transport (python-can `serial` bus on a pty),
`umcub_lite.py` on bootloaders without SMP (stream and CAN).

The bench tools reach the target through `tools/hw/probe.py` (a thin SWD layer): `--probe stm32cube` uses
STM32_Programmer_CLI (`$STM32_PROGRAMMER_CLI` or PATH), `--probe pyocd` uses pyOCD (`--target`, `--pack` for the
part); `$UMCUB_PROBE` sets the default. `probe.py flash a.hex b.bin@0x08008000` programs and resets.

Stack use on the board: `tools/hw/stack_usage.py paint`, run a scenario, `tools/hw/stack_usage.py measure`
(deepest use since the paint; `loop-app` gives a signed application that does not touch the RAM, for boot paths).

Hardware tests: `tools/hw/powerfail_test.py` resets the MCU in the middle of the K-th flash operation (build with
`tests/configs/feature/fault_inject.h`, test only) and checks that an interrupted upgrade or revert (swap modes) or an
interrupted overwrite copy always completes. The images are signed with `tools/host/umcub_image.py` for the
bootloader's board and overlays; the fault record (`UMCUB_CFG_TEST_FAULT_ADDR`, a RAM word that survives a reset) and
the handoff area come from that configuration too. A run ends when the bootloader publishes the handoff (right before
the jump); with `--sector-size` only the sectors that programming does not rewrite are erased explicitly.
`--encrypted` (bootloader with encrypted images) installs an encrypted update, so every resumed swap has to recover
the image key from the trailer.
`TODO.md` lists what is still open on hardware.
