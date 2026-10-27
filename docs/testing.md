# Testing

The build matrix, host tests, simulated devices and the hardware tests.

```sh
tools/build/build_matrix.sh                 # 51 bootloader configurations, presets, examples, IDE checks, host
                                            # tests, link e2e; warning-free; CTest underneath (JOBS=n), size summary
tools/build/build_matrix.sh build/matrix -R bluepill   # some tests (any ctest option after the directory)
ctest --test-dir build/matrix -j4 --rerun-failed --output-on-failure   # the same tests with ctest directly
(cd tests && cmake --preset matrix && ctest --preset matrix)          # tests/CMakePresets.json, e.g. for an IDE
ctest --test-dir build/matrix/host    # host tests only
tests/host/link_e2e.py build/matrix/host   # simulated devices: umcub link bus, smpmgr, host tools, lite upload
tests/tools/check_pins_test.py              # pin check: right / wrong pins and AF numbers, AF filled in, packages
tools/build/check_docs.py                   # README and docs/ still match the repository (size tables, links, ...)
tools/build/check_config.py build/matrix/*/ # CMake and umcub/config.py read the same values and slot windows
```

The matrix is a CTest project (`tests/CMakeLists.txt`): one test per bootloader configuration in
`tests/matrix/configs.txt` (`name|core|build type|pre overlay|post overlay|board`), per CMake preset and per example
application, plus the checks (layers, pin check, IDE library compile, CMake and host tools reading the same
configuration), the host tests and the link e2e. `tests/matrix/run.py` runs one test; logs are
`build/matrix/<name>.*.log`, build trees `build/matrix/<name>/`; `ctest -R`, `--rerun-failed` and `--output-junit`
work as usual. In VS Code (CMake Tools) the matrix is a second project next to the bootloader:
`"cmake.sourceDirectory": ["${workspaceFolder}", "${workspaceFolder}/tests"]` and
`"cmake.ctest.testExplorerIntegrationEnabled": true` in `.vscode/settings.json`, then the `matrix` preset of the
`tests` project lists every test in the Testing view.

Host tests (`tests/host`) run the real MCUboot (boot_go, boot_serial, ECDSA) on an emulated H7 flash (32-byte
words, no double programming) through the umcub transport layer. Covered: SMP upload / echo / list over the packet
and stream paths (including two interleaved streams), swap → revert → confirm in scratch / offset modes, overwrite
(installed for good, an older image too, no revert), direct-xip (the newest valid slot runs, a damaged or misplaced
image is erased, fallback to the other slot), downgrade prevention, unsigned images (`UMCUB_SIGN_NONE`), SMP
upload into the secondary slot (`UMCUB_CFG_SMP_DIRECT_UPLOAD`) marked by `image state-write` for a test or for good, text
commands, verify / hash / read, ISO-TP classic/FD, DHCP / ARP / ICMP, board-supplied drivers, transports and protocol frames (packets and stream lines), the
status LED patterns (listening, data, a failed transport init), the
board-type check, the umcub link in ADDRESSED mode and in SECURE mode with encryption (handshake, wrong admin key,
replayed / tampered / reordered frames, session end, RDP level 0), encrypted images (SMP upload into the primary
slot decrypted in place, swap upgrade / revert / confirm with re-encryption, verify of an encrypted secondary,
readback only inside an encrypted session; with two images an encrypted upload into image 1's
primary slot refused, through its secondary slot decrypted by the swap). `link_e2e.py` runs device simulators (the real mux, link, boot_serial and
CAN transport on emulated flash) against the host tools: the `umcub` tool with three link nodes on one bus (also SMP upload
through a SECURE link), on plain SMP, on the CAN transport (python-can `serial` bus on a pty) and on
bootloaders without SMP (lite upload, stream and CAN); `smpmgr` directly and through `umcub serve`.

The bench tools reach the target through `tools/hw/probe.py` (a thin SWD layer): `--probe stm32cube` uses
STM32_Programmer_CLI (`$STM32_PROGRAMMER_CLI` or PATH), `--probe pyocd` uses pyOCD (`--target`, `--pack` for the
part); `$UMCUB_PROBE` sets the default. `probe.py flash a.hex b.bin@0x08008000` programs and resets.

Stack use on the board: `tests/hw/stack_usage.py paint`, run a scenario, `tests/hw/stack_usage.py measure`
(deepest use since the paint; `loop-app` gives a signed application that does not touch the RAM, for boot paths).

Entropy of the STM32F1 source (no TRNG): `tests/hw/entropy_test.py --serial <probe>` loads a sampler into SRAM
(nothing written to flash), collects the raw ADC conversions of one long run and of fresh starts, and prints
min-entropy estimates (SP 800-90B most-common-value and Markov) per conversion and per 32-byte block, what
SysTick adds, and whether the first block after start-up repeats. Blue Pill at room temperature: 60-80 bits per
block, SysTick nothing.

Hardware tests: `tests/hw/powerfail_test.py` resets the MCU in the middle of the K-th flash operation (build with
`tests/configs/feature/fault_inject.h`: `UMCUB_CFG_TEST_FAULT_INJECT 1`, test only) and checks that an interrupted upgrade or revert (swap modes) or an
interrupted overwrite copy always completes. The images are signed with `umcub image sign` for the
bootloader's board and overlays; the fault record (`UMCUB_CFG_TEST_FAULT_ADDR`, a RAM word that survives a reset) and
the handoff area come from that configuration too. A run ends when the bootloader publishes the handoff (right before
the jump); with `--sector-size` only the sectors that programming does not rewrite are erased explicitly.
`--encrypted` (bootloader with encrypted images) installs an encrypted update, so every resumed swap has to recover
the image key from the trailer.
`TODO.md` lists what is still open on hardware.
